// Minimal educational glTF 2.0 loader (C11).
//
// Scene graph evaluation: compute node world matrices for a scene.
//
// Responsibilities:
//   - allocate/free a reusable world-matrix cache
//   - compute world matrices for nodes reachable from scene roots
//   - expose helper APIs:
//       * gltf_node_local_matrix()  (TRS/matrix -> mat4)
//       * gltf_world_matrix()       (read computed world mat4)
//
// Notes:
//   - World matrices are computed only for nodes reachable from scene roots.
//   - A node can exist in doc->nodes[] but remain UNVISITED for a scene.
//   - Matrix convention is column-major: m[col*4 + row] (OpenGL/glTF).
//
// Public API contracts are documented in include/gltf/gltf.h.


#include "gltf_internal.h"
#include "gltf_math.h"


// ----------------------------------------------------------------------------
// World cache lifecycle
// ----------------------------------------------------------------------------


gltf_result gltf_world_cache_create(const gltf_doc* doc,
                                    gltf_world_cache** out_cache,
                                    gltf_error* out_err) {
  if (!doc || !out_cache) {
    gltf_set_err_if(out_err, "invalid args", NULL, 0, 0);
    return GLTF_ERR_INVALID;
  }

  gltf_world_cache* cache = (gltf_world_cache*)calloc(1, sizeof(*cache));
  if (!cache) {
    gltf_set_err_if(out_err, "oom", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  cache->node_count = doc->node_count;
  cache->scene_index = UINT32_MAX;
  cache->valid = 0;

  cache->state = (uint8_t*)malloc((size_t)doc->node_count *
                                  sizeof(cache->state[0]));
  if (!cache->state) {
    free(cache);
    gltf_set_err_if(out_err, "oom", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  cache->world_m16 = (float*)malloc((size_t)doc->node_count * 16u *
                                    sizeof(cache->world_m16[0]));
  if (!cache->world_m16) {
    free(cache->state);
    free(cache);
    gltf_set_err_if(out_err, "oom", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  memset(cache->state, 0, (size_t)doc->node_count * sizeof(cache->state[0]));
  *out_cache = cache;

  return GLTF_OK;
}

void gltf_world_cache_free(gltf_world_cache* cache) {
  if (!cache) return;
  free(cache->world_m16);
  free(cache->state);
  free(cache);
}


// ----------------------------------------------------------------------------
// World cache accessors (internal helpers)
// ----------------------------------------------------------------------------


static inline float* world_ptr(gltf_world_cache* c, uint32_t node) {
  return &c->world_m16[(size_t)node * 16u];
}

static inline const float* world_ptr_ro(const gltf_world_cache* c,
                                        uint32_t node) {
  return &c->world_m16[(size_t)node * 16u];
}


// ----------------------------------------------------------------------------
// Public helpers: world matrix getter + local matrix builder
// ----------------------------------------------------------------------------


int gltf_world_matrix(const gltf_doc* doc,
                      const gltf_world_cache* cache,
                      uint32_t node_index,
                      float out_m16[16]) {
  if (!doc || !cache || !out_m16) return 0;
  if (node_index >= doc->node_count) return 0;
  if (cache->node_count != doc->node_count) return 0;
  if (!cache->valid) return 0;
  if (!cache->state || !cache->world_m16) return 0;

  // Return only if the node was actually computed for this scene.
  // DONE == 2 by convention.
  if (cache->state[node_index] != 2u) return 0;

  const float* src = world_ptr_ro(cache, node_index);
  memcpy(out_m16, src, 16u * sizeof(float));

  return 1;
}

int gltf_node_local_matrix(const gltf_doc* doc,
                           uint32_t node_index,
                           float out_m16[16]) {
  if (!doc || !out_m16) return 0;
  if (node_index >= doc->node_count) return 0;

  const gltf_node* n = &doc->nodes[node_index];

  // If node.matrix is present, TRS must be ignored (glTF spec).
  if (n->has_matrix) {
    memcpy(out_m16, n->matrix, 16u * sizeof(float));
    return 1;
  }

  // Build local = T * R * S (glTF node TRS).
  //
  // Implementation strategy:
  //   1) start with R
  //   2) apply S into basis columns
  //   3) set translation column
  mat4_from_quat(out_m16, n->rotation);
  mat4_apply_scale(out_m16, n->scale);
  mat4_apply_translation(out_m16, n->translation);

  return 1;
}


// ----------------------------------------------------------------------------
// DFS (iterative) evaluation
// ----------------------------------------------------------------------------


typedef struct gltf_dfs_frame {
  uint32_t node;     // current node being processed
  uint32_t parent;   // parent node index, or UINT32_MAX if root
  uint32_t child_i;  // next child index to process (0..children.count)
} gltf_dfs_frame;

static inline void stack_push(gltf_dfs_frame* stack,
                              uint32_t* top,
                              gltf_dfs_frame f) {
  stack[(*top)++] = f;
}

static inline gltf_dfs_frame* stack_peek(gltf_dfs_frame* stack,
                                         uint32_t top) {
  return &stack[top - 1];
}

static inline void stack_pop(uint32_t* top) {
  (*top)--;
}

// Evaluate one node: compute world = parent_world * local(node).
//
// State machine:
//   - UNVISITED (0) -> VISITING (1) when world matrix is written
//   - VISITING (1)  indicates a cycle if seen again while on stack
//   - DONE (2)      is set when all children are processed (dfs_step exit)
static gltf_result eval_one_node(const gltf_doc* doc,
                                 gltf_world_cache* cache,
                                 uint32_t node,
                                 const float parent_world[16],
                                 gltf_error* out_err) {
  if (!doc || !cache || !parent_world) {
    gltf_set_err_if(out_err, "invalid arguments", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (node >= doc->node_count) {
    gltf_set_err_if(out_err, "node index out of range", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }
  
  const uint8_t node_state = cache->state[node];

  if (node_state == 2u) return GLTF_OK; // DONE
  if (node_state == 1u) {               // VISITING -> cycle
    gltf_set_err_if(out_err, "cycle in node graph", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  // UNVISITED -> VISITING (world becomes available for children).
  cache->state[node] = 1u;

  float local[16];
  if (!gltf_node_local_matrix(doc, node, local)) {
    gltf_set_err_if(out_err, "failed to compute local matrix", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  float* out_world = world_ptr(cache, node);
  mat4_mul(parent_world, local, out_world);

  return GLTF_OK;
}

// One iterative DFS step on the explicit stack.
//
// Behavior:
//   - If frame.node is UNVISITED: compute its world matrix (VISITING).
//   - Then iterate its children one by one by pushing new frames.
//   - When all children processed: mark DONE and pop the frame.
static gltf_result dfs_step(const gltf_doc* doc,
                            gltf_world_cache* cache,
                            gltf_dfs_frame* stack,
                            uint32_t* top,
                            const float identity[16],
                            gltf_error* out_err) {
  if (!doc || !cache || !stack || !top || !identity) {
    gltf_set_err_if(out_err, "invalid arguments", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (*top == 0) {
    gltf_set_err_if(out_err, "empty stack", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  gltf_dfs_frame* f = stack_peek(stack, *top);
  uint32_t node = f->node;

  if (cache->state[node] == 0u) {
    const float* parent_world = f->parent == UINT32_MAX
                                  ? identity
                                  : world_ptr_ro(cache, f->parent);
    return eval_one_node(doc, cache, node, parent_world, out_err);
  }

  // DESCEND / EXIT
  gltf_range_u32 cr = doc->nodes[node].children;
  if (cr.first > doc->indices_count ||
      cr.count > doc->indices_count - cr.first) {
    gltf_set_err_if(out_err, "childen range out of bounds", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  if (f->child_i < cr.count) {
    uint32_t child = doc->indices_u32[cr.first + f->child_i];
    f->child_i++;

    if (child >= doc->node_count) {
      gltf_set_err_if(out_err, "child index out of range", NULL, 1, 1);
      return GLTF_ERR_INVALID;
    }

    stack_push(stack, top, (gltf_dfs_frame){
      .node = child,
      .parent = node,
      .child_i = 0u,
    });
    return GLTF_OK;
  }

  cache->state[node] = 2u; // DONE
  stack_pop(top);
  return GLTF_OK;
}


// ----------------------------------------------------------------------------
// Public API: compute world matrices for a scene
// ----------------------------------------------------------------------------


gltf_result gltf_compute_world_matrices(const gltf_doc* doc,
                                        uint32_t scene_index,
                                        gltf_world_cache* cache,
                                        gltf_error* out_err) {
  if (!doc || !cache) {
    gltf_set_err_if(out_err, "invalid args", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (cache->node_count != doc->node_count) {
    gltf_set_err_if(out_err, "cache/doc node_count mismatch", NULL, 0, 0);
    return GLTF_ERR_INVALID;
  }
  if (scene_index >= doc->scene_count) {
    gltf_set_err_if(out_err, "scene index out of range", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  cache->valid = 0;
  cache->scene_index = scene_index;
  memset(cache->state, 0, (size_t)doc->node_count * sizeof(cache->state[0]));

  const gltf_scene* scene = &doc->scenes[scene_index];

  if (!scene->nodes.count) {
    cache->valid = 1;
    return GLTF_OK;
  }

  uint32_t node_offset = scene->nodes.first;
  uint32_t node_count = scene->nodes.count;

  if (node_offset > doc->indices_count ||
      node_count > doc->indices_count - node_offset) {
    gltf_set_err_if(out_err, "scene.nodes out of bounds", NULL, 1, 1);
    return GLTF_ERR_INVALID;
  }

  float I[16];
  mat4_identity(I);

  gltf_dfs_frame* stack = (gltf_dfs_frame*)malloc(sizeof(*stack) * (size_t)doc->node_count);
  if (!stack) {
    gltf_set_err_if(out_err, "oom", NULL, 1, 1);
    return GLTF_ERR_IO;
  }

  gltf_result r = GLTF_OK;
 
  // Run a separate DFS for each scene root.
  for (uint32_t i = 0; i < node_count; i++) {
    uint32_t top = 0;
    uint32_t node_index = doc->indices_u32[node_offset + i];

    if (node_index >= doc->node_count) {
      gltf_set_err_if(out_err, "root node index out of range", NULL, 0, 0);
      r = GLTF_ERR_INVALID;
      break;
    }

    // If this root was already processed by a previous root traversal,
    // we can skip it (prevents extra stack churn in weird scenes).
    if (cache->state[node_index] == 2u) continue;

    stack_push(stack, &top, (gltf_dfs_frame){
      .node = node_index,
      .parent = UINT32_MAX,
      .child_i = 0u,
    });

    while (top > 0) {
      r = dfs_step(doc, cache, stack, &top, I, out_err);
      if (r != GLTF_OK) break;
    }
    if (r != GLTF_OK) break;
  }

  free(stack);

  if (r == GLTF_OK) cache->valid = 1;
  return r;
}
