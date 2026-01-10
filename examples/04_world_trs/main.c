#include "gltf/gltf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void mat4_extract_translation(const float m[16], float out_t3[3]) {
  out_t3[0] = m[12];
  out_t3[1] = m[13];
  out_t3[2] = m[14];
}

// Robust scale: lengths of basis columns (works with rotation + non-uniform scale)
static void mat4_extract_scale(const float m[16], float out_s3[3]) {
  const float x0 = m[0],  x1 = m[1],  x2 = m[2];
  const float y0 = m[4],  y1 = m[5],  y2 = m[6];
  const float z0 = m[8],  z1 = m[9],  z2 = m[10];

  out_s3[0] = sqrtf(x0*x0 + x1*x1 + x2*x2);
  out_s3[1] = sqrtf(y0*y0 + y1*y1 + y2*y2);
  out_s3[2] = sqrtf(z0*z0 + z1*z1 + z2*z2);
}

static void indent(int depth) {
  for (int i = 0; i < depth; ++i) printf("  ");
}

static void print_node_line(const gltf_doc* doc,
                            uint32_t node_index,
                            int depth,
                            const float local[16],
                            const float world[16],
                            int has_world) {
  float lt[3], ls[3], wt[3], ws[3];

  mat4_extract_translation(local, lt);
  mat4_extract_scale(local, ls);

  indent(depth);
  printf("- node[%u] name='%s' mesh=%d\n",
         node_index,
         gltf_doc_node_name(doc, node_index) ? gltf_doc_node_name(doc, node_index) : "(null)",
         (int)gltf_doc_node_mesh(doc, node_index));

  indent(depth + 1);
  printf("local T=(%.3f %.3f %.3f)  S=(%.3f %.3f %.3f)\n",
         lt[0], lt[1], lt[2], ls[0], ls[1], ls[2]);

  if (has_world) {
    mat4_extract_translation(world, wt);
    mat4_extract_scale(world, ws);

    indent(depth + 1);
    printf("world T=(%.3f %.3f %.3f)  S=(%.3f %.3f %.3f)\n",
           wt[0], wt[1], wt[2], ws[0], ws[1], ws[2]);
  } else {
    indent(depth + 1);
    printf("world (not computed / unreachable)\n");
  }
}

// Iterative DFS printing, using the public child API
typedef struct frame {
  uint32_t node;
  uint32_t next_child;
  int depth;
} frame;

static int dump_scene_hierarchy(const gltf_doc* doc,
                                uint32_t scene_index,
                                const gltf_world_cache* cache) {
  uint32_t root_count = gltf_doc_scene_node_count(doc, scene_index);
  if (root_count == 0) {
    printf("scene[%u] has no root nodes\n", scene_index);
    return 1;
  }

  // worst-case stack size = node_count
  uint32_t node_count = gltf_doc_node_count(doc);
  frame* st = (frame*)malloc((size_t)node_count * sizeof(frame));
  if (!st) return 0;

  for (uint32_t r = 0; r < root_count; ++r) {
    uint32_t root = 0;
    if (!gltf_doc_scene_node(doc, scene_index, r, &root)) continue;

    uint32_t top = 0;
    st[top++] = (frame){ .node = root, .next_child = 0, .depth = 0 };

    while (top > 0) {
      frame* f = &st[top - 1];

      // On first time we see this frame, print it
      if (f->next_child == 0) {
        float local[16];
        float world[16];
        int has_local = gltf_node_local_matrix(doc, f->node, local);

        int has_world = 0;
        if (cache) {
          has_world = gltf_world_matrix(doc, cache, f->node, world);
        }

        if (!has_local) {
          indent(f->depth);
          printf("- node[%u] <failed to compute local>\n", f->node);
        } else {
          print_node_line(doc, f->node, f->depth, local, world, has_world);
        }
      }

      uint32_t child_count = gltf_doc_node_child_count(doc, f->node);
      if (f->next_child < child_count) {
        uint32_t child = 0;
        if (!gltf_doc_node_child(doc, f->node, f->next_child, &child)) {
          // invalid child fetch -> stop this node
          top--;
          continue;
        }
        f->next_child++;

        st[top++] = (frame){ .node = child, .next_child = 0, .depth = f->depth + 1 };
        continue;
      }

      // done with this node
      top--;
    }
  }

  free(st);
  return 1;
}

int main(int argc, char** argv) {
  const char* path = "tests/fixtures/04-world_trs.gltf";
  if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
    path = argv[1];
  }

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  int rc = gltf_load_file(path, &doc, &err);
  if (rc != GLTF_OK) {
    const char* msg = err.message ? err.message : "unknown error";
    const char* epath = err.path ? err.path : "";
    int line = err.line ? err.line : 1;
    int col  = err.col  ? err.col  : 1;

    fprintf(stderr, "ERROR: %s at %d:%d path=%s\n", msg, line, col, epath);
    return 1;
  }

  printf("asset.version=%s, scenes=%u, nodes=%u, meshes=%u\n",
         gltf_doc_asset_version(doc),
         gltf_doc_scene_count(doc),
         gltf_doc_node_count(doc),
         gltf_doc_mesh_count(doc));

  // Create cache and compute world matrices for selected scene
  gltf_world_cache* cache = NULL;
  rc = gltf_world_cache_create(doc, &cache, &err);
  if (rc != GLTF_OK) {
    fprintf(stderr, "ERROR: gltf_world_cache_create failed: %s\n",
            err.message ? err.message : "unknown");
    gltf_free(doc);
    return 1;
  }

  uint32_t scene_index =
    (gltf_doc_default_scene(doc) >= 0) ? (uint32_t)gltf_doc_default_scene(doc) : 0u;

  rc = gltf_compute_world_matrices(doc, scene_index, cache, &err);
  if (rc != GLTF_OK) {
    fprintf(stderr, "ERROR: gltf_compute_world_matrices failed: %s\n",
            err.message ? err.message : "unknown");
    gltf_world_cache_free(cache);
    gltf_free(doc);
    return 1;
  }

  printf("\nscene[%u] hierarchy:\n", scene_index);
  if (!dump_scene_hierarchy(doc, scene_index, cache)) {
    fprintf(stderr, "ERROR: dump_scene_hierarchy OOM\n");
    gltf_world_cache_free(cache);
    gltf_free(doc);
    return 1;
  }

  gltf_world_cache_free(cache);
  gltf_free(doc);
  return 0;
}
