#include "gltf/gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

#define GLTF_STR_INVALID_OFF           0xFFFFFFFFu
#define GLTF_ARENA_INITIAL_CAPACITY    (16 * 1024)
#define GLTF_INDICES_INITIAL_CAP       256u
#define GLTF_DOC_DEFAULT_SCENE_INVALID -1

// Half-open range into a shared uint32 index pool:
// indices are stored in doc->indices_u32[first .. first+count).
// Used to represent variable-length arrays without per-object allocations.
typedef struct gltf_range_u32 {
  uint32_t first;
  uint32_t count;
} gltf_range_u32;

// Simple growable byte arena.
// Owns a contiguous buffer used to store all strings (and possibly other small blobs later).
// Invariant: arena.size <= arena.cap. arena.data may be NULL when cap==0.
typedef struct gltf_arena {
  uint8_t* data;
  size_t   size; // bytes used
  size_t   cap;  // bytes allocated
} gltf_arena;

// String view into doc->arena.
// off/len reference a NUL-terminated string stored at arena.data+off,
// with length len excluding the trailing '\0'.
// Invalid string is represented by off == GLTF_STR_INVALID_OFF.
typedef struct gltf_str {
  uint32_t off; // byte offset in arena->data
  uint32_t len; // not including '\0'
} gltf_str;

// Parsed glTF scene (Iteration 1 subset).
// nodes: root node indices for this scene (into doc->indices_u32).
typedef struct gltf_scene {
  gltf_str name;        // optional
  gltf_range_u32 nodes; // root node indices (variable length)
} gltf_scene;

// Parsed glTF node (Iteration 1 subset).
// mesh: index into doc->meshes, or -1 if absent.
// children: child node indices (into doc->indices_u32).
typedef struct gltf_node {
  gltf_str name;           // optional
  int32_t  mesh;           // -1 if absent
  gltf_range_u32 children; // child node indices (variable length)
} gltf_node;

// Parsed glTF mesh (Iteration 1 subset).
// Primitives/attributes are parsed in later iterations.
typedef struct gltf_mesh {
  gltf_str name; // optional
} gltf_mesh;

// Internal document layout (opaque to users).
//
// Design goals:
// - Stable public ABI: users access data via count/get accessors only.
// - Performance: store all names in one arena, and all variable-length index arrays
//   in one shared u32 pool to avoid many small allocations.
// - Memory safety: all owned allocations are reachable from gltf_doc and released by gltf_free().
struct gltf_doc {
  // asset.version is small; store inline for quick access and no arena lookup.
  char asset_version[8];

  // Optional asset.generator string stored in arena (invalid if absent).
  gltf_str asset_generator;

  // Default scene index from top-level "scene" property, or GLTF_DOC_DEFAULT_SCENE_INVALID.
  int32_t default_scene;

  // Top-level array counts (0 if absent).
  uint32_t scene_count;
  uint32_t node_count;
  uint32_t mesh_count;

  // Parsed arrays (owned).
  gltf_scene* scenes; // [scene_count]
  gltf_node*  nodes;  // [node_count]
  gltf_mesh*  meshes; // [mesh_count]

  // Shared index pool for all variable-length arrays (owned).
  // Currently used by:
  // - scenes[i].nodes
  // - nodes[i].children
  uint32_t* indices_u32;
  uint32_t  indices_count; // number of valid entries
  uint32_t  indices_cap;   // allocated entries

  // Arena storage for all strings (owned).
  gltf_arena arena;
};

static inline gltf_str gltf_str_invalid(void) {
  gltf_str s = { GLTF_STR_INVALID_OFF, 0 };
  return s;
}

static inline int gltf_str_is_valid(gltf_str s) {
  return s.off != GLTF_STR_INVALID_OFF;
}

void arena_init(gltf_arena* arena, size_t initial_cap) {
  if (initial_cap == 0) initial_cap = GLTF_ARENA_INITIAL_CAPACITY;
  arena->data = (uint8_t*)malloc(initial_cap);
  arena->size = 0;
  arena->cap = arena->data ? initial_cap : 0;
}

int arena_reserve(gltf_arena* arena, size_t additional_bytes) {
  if (additional_bytes > SIZE_MAX - arena->size) return 0;

  const size_t required = arena->size + additional_bytes;

  if (required > UINT32_MAX) return 0;
  if (required <= arena->cap) return 1;

  size_t new_cap = arena->cap ? arena->cap : GLTF_ARENA_INITIAL_CAPACITY;
  while (new_cap < required) {
    if (new_cap > SIZE_MAX / 2) {
      new_cap = required;
      break;
    }
    new_cap *= 2;
  }

  uint8_t* new_data = (uint8_t*)realloc(arena->data, new_cap);
  if (!new_data) return 0;

  arena->data = new_data;
  arena->cap = new_cap;
  return 1;
}

gltf_str arena_strdup(gltf_arena* arena, const char* s) {
  if (!s) {
    return gltf_str_invalid();
  }
  size_t len = strlen(s);
  if (arena_reserve(arena, len + 1) == 0) {
    return gltf_str_invalid();
  }
  uint32_t off = (uint32_t)arena->size;
  memcpy(&arena->data[arena->size], s, len + 1);
  arena->size += len + 1;
  gltf_str result = {off, (uint32_t)len};
  return result;
}

const char* arena_get_str(const gltf_arena* arena, gltf_str s) {
  if (!gltf_str_is_valid(s)) return NULL;
  if ((size_t)s.off + (size_t)s.len + 1 > arena->size) return NULL;
  return (const char*)(arena->data + s.off);
}

static int indices_reserve(gltf_doc* doc, uint32_t additional) {
  if (additional > UINT32_MAX - doc->indices_count) return 0;

  uint32_t required = doc->indices_count + additional;
  
  if (required <= doc->indices_cap) return 1;

  uint32_t new_cap = doc->indices_cap ? doc->indices_cap : GLTF_INDICES_INITIAL_CAP;
  while (new_cap < required) {
    if (new_cap > UINT32_MAX / 2) {
      new_cap = required;
      break;
    }
    new_cap *= 2;
    if ((size_t)new_cap > SIZE_MAX / sizeof(uint32_t)) return 0;
  }

  size_t bytes = (size_t)new_cap * sizeof(uint32_t);
  uint32_t* p = (uint32_t*)realloc(doc->indices_u32, bytes);
  if (!p) return 0;

  doc->indices_u32 = p;
  doc->indices_cap = new_cap;
  return 1;
}

static int indices_push_u32(gltf_doc* doc, uint32_t v) {
  if (!indices_reserve(doc, 1)) return 0;
  doc->indices_u32[doc->indices_count++] = v;
  return 1;
}

static void gltf_set_err(gltf_error* out_err,
                         const char* message,
                         const char* path,
                         int line,
                         int col) {
  if (!out_err) {
    return;
  }
  out_err->message = message;
  out_err->path = path;
  out_err->line = line;
  out_err->col = col;
}

gltf_result gltf_load_file(const char* path,
                   gltf_doc** out_doc,
                   gltf_error* out_err) {
#define fail(_code, ...) do { \
  gltf_set_err(out_err, __VA_ARGS__); \
  yyjson_doc_free(json_doc); \
  gltf_free(doc); \
  return _code; \
} while (0)

  gltf_doc* doc = NULL;
  yyjson_read_err err;
  yyjson_doc *json_doc = NULL;

  if (out_doc) {
    *out_doc = NULL;
  }

  if (!path || !out_doc) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  json_doc = yyjson_read_file(path, 0, NULL, &err);

  if (!json_doc) {
    fail(GLTF_ERR_PARSE, err.msg, "root", 1, 1);
  }

  doc = (gltf_doc*)calloc(1, sizeof(gltf_doc));
  if (!doc) {
    fail(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  arena_init(&doc->arena, GLTF_ARENA_INITIAL_CAPACITY);

  // if (!doc->arena.data) {
  //   fail(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  // }

  // Root
  yyjson_val *root = yyjson_doc_get_root(json_doc);
  if (!root || !yyjson_is_obj(root)) {
    fail(GLTF_ERR_PARSE, "root must be an object", "root", 1, 1);
  }

  // Default scene
  yyjson_val *default_scene_val = yyjson_obj_get(root, "scene");
  if (default_scene_val) {
    if (!yyjson_is_uint(default_scene_val)) {
      fail(GLTF_ERR_PARSE, "root.scene must be an unsigned integer", "root.scene", 1, 1);
    }
    uint64_t s = (uint64_t)yyjson_get_uint(default_scene_val);
    if (s > (uint64_t)INT32_MAX) {
      fail(GLTF_ERR_PARSE, "root.scene is too large", "root.scene", 1, 1);
    }
    doc->default_scene = (int32_t)s;
  } else {
    doc->default_scene = GLTF_DOC_DEFAULT_SCENE_INVALID;
  }

  // Scenes
  yyjson_val *scenes_val = yyjson_obj_get(root, "scenes");
  if (!scenes_val) {
    doc->scene_count = 0;
  } else if (yyjson_is_arr(scenes_val)) {
    doc->scene_count = (unsigned)yyjson_arr_size(scenes_val);
  } else {
    fail(GLTF_ERR_PARSE, "root.scenes must be an array", "root.scenes", 1, 1);
  }

  if (doc->scene_count > 0) {
    doc->scenes = (gltf_scene*)malloc(sizeof(gltf_scene) * doc->scene_count);
    if (!doc->scenes) {
      fail(GLTF_ERR_IO, "out of memory", "root.scenes", 1, 1);
    }
    memset(doc->scenes, 0, sizeof(gltf_scene) * doc->scene_count);

    size_t scene_idx, max;
    yyjson_val *scene_val = NULL;
    yyjson_arr_foreach(scenes_val, scene_idx, max, scene_val) {
      if (!yyjson_is_obj(scene_val)) {
        fail(GLTF_ERR_PARSE, "scene must be an object", "root.scenes[]", 1, 1);
      }

      yyjson_val* name = yyjson_obj_get(scene_val, "name");
      if (!name) {
        doc->scenes[scene_idx].name = gltf_str_invalid();
      } else if (!yyjson_is_str(name)) {
        fail(GLTF_ERR_PARSE, "must be string", "root.scenes[].name", 1, 1);
      } else {
        const char* name_str = yyjson_get_str(name);
        if (name_str != NULL) {
          doc->scenes[scene_idx].name = arena_strdup(&doc->arena, name_str);
          if (!gltf_str_is_valid(doc->scenes[scene_idx].name)) {
            fail(GLTF_ERR_IO, "out of memory", "root.scenes[].name", 1, 1);
          }
        }
      }

      yyjson_val* nodes_val = yyjson_obj_get(scene_val, "nodes");
      if (nodes_val) {
        if (!yyjson_is_arr(nodes_val)) {
          fail(GLTF_ERR_PARSE, "must be array", "root.scenes[].nodes", 1, 1);
        } else {
          size_t node_idx, nmax;
          yyjson_val* node_val = NULL;
          uint32_t first_index = doc->indices_count;
          yyjson_arr_foreach(nodes_val, node_idx, nmax, node_val) {
            if (!yyjson_is_uint(node_val)) {
              fail(GLTF_ERR_PARSE, "must be unsigned integer", "root.scenes[].nodes[]", 1, 1);
            }
            uint32_t node_index = (uint32_t)yyjson_get_uint(node_val);
            if (!indices_push_u32(doc, node_index)) {
              fail(GLTF_ERR_IO, "out of memory", "root.scenes[].nodes[]", 1, 1);
            }
          }
          doc->scenes[scene_idx].nodes.first = first_index;
          doc->scenes[scene_idx].nodes.count = (uint32_t)(doc->indices_count - first_index);
        }
      } else {
        doc->scenes[scene_idx].nodes.first = 0;
        doc->scenes[scene_idx].nodes.count = 0;
      }
    }
  }

  // Nodes
  yyjson_val *nodes_val = yyjson_obj_get(root, "nodes");
  if (!nodes_val) {
    doc->node_count = 0;
  } else if (yyjson_is_arr(nodes_val)) {
    doc->node_count = (unsigned)yyjson_arr_size(nodes_val);
  } else {
    fail(GLTF_ERR_PARSE, "root.nodes must be an array", "root.nodes", 1, 1);
  }

  if (doc->node_count > 0) {
    doc->nodes = (gltf_node*)malloc(sizeof(gltf_node) * doc->node_count);
    if (!doc->nodes) {
      fail(GLTF_ERR_IO, "out of memory", "root.nodes", 1, 1);
    }
    memset(doc->nodes, 0, sizeof(gltf_node) * doc->node_count);

    size_t node_idx, max;
    yyjson_val *node_val = NULL;
    yyjson_arr_foreach(nodes_val, node_idx, max, node_val) {
      if (!yyjson_is_obj(node_val)) {
        fail(GLTF_ERR_PARSE, "node must be an object", "root.nodes[]", 1, 1);
      }
      
      yyjson_val* name = yyjson_obj_get(node_val, "name");
      if (!name) {
        doc->nodes[node_idx].name = gltf_str_invalid();
      } else if (!yyjson_is_str(name)) {
        fail(GLTF_ERR_PARSE, "must be string", "root.nodes[].name", 1, 1);
      } else {
        const char* name_str = yyjson_get_str(name);
        if (name_str != NULL) {
          doc->nodes[node_idx].name = arena_strdup(&doc->arena, name_str);
          if (!gltf_str_is_valid(doc->nodes[node_idx].name)) {
            fail(GLTF_ERR_IO, "out of memory", "root.nodes[].name", 1, 1);
          }
        }
      }

      yyjson_val* mesh = yyjson_obj_get(node_val, "mesh");
      if (!mesh) {
        doc->nodes[node_idx].mesh = -1;
      } else if (!yyjson_is_int(mesh)) {
        fail(GLTF_ERR_PARSE, "must be integer", "root.nodes[].mesh", 1, 1);
      } else {
        int32_t mesh_index = (int32_t)yyjson_get_int(mesh);
        doc->nodes[node_idx].mesh = mesh_index;
      }
    }
  }

  // Meshes
  yyjson_val *meshes_val = yyjson_obj_get(root, "meshes");
  if (!meshes_val) {
    doc->mesh_count = 0;
  } else if (yyjson_is_arr(meshes_val)) {
    doc->mesh_count = (unsigned)yyjson_arr_size(meshes_val);
  } else {
    fail(GLTF_ERR_PARSE, "root.meshes must be an array", "root.meshes", 1, 1);
  }

  if (doc->mesh_count > 0) {
    doc->meshes = (gltf_mesh*)malloc(sizeof(gltf_mesh) * doc->mesh_count);
    if (!doc->meshes) {
      fail(GLTF_ERR_IO, "out of memory", "root.meshes", 1, 1);
    }
    memset(doc->meshes, 0, sizeof(gltf_mesh) * doc->mesh_count);

    size_t mesh_idx, max;
    yyjson_val *mesh_val = NULL;
    yyjson_arr_foreach(meshes_val, mesh_idx, max, mesh_val) {
      if (!yyjson_is_obj(mesh_val)) {
        fail(GLTF_ERR_PARSE, "mesh must be an object", "root.meshes[]", 1, 1);
      }
      
      yyjson_val* name = yyjson_obj_get(mesh_val, "name");
      if (!name) {
        doc->meshes[mesh_idx].name = gltf_str_invalid();
      } else if (!yyjson_is_str(name)) {
        fail(GLTF_ERR_PARSE, "must be string", "root.meshes[].name", 1, 1);
      } else {
        const char* name_str = yyjson_get_str(name);
        if (name_str != NULL) {
          doc->meshes[mesh_idx].name = arena_strdup(&doc->arena, name_str);
          if (!gltf_str_is_valid(doc->meshes[mesh_idx].name)) {
            fail(GLTF_ERR_IO, "out of memory", "root.meshes[].name", 1, 1);
          }
        }
      }
    }
  }

  yyjson_val *asset_val = yyjson_obj_get(root, "asset");
  if (!asset_val || !yyjson_is_obj(asset_val)) {
    fail(GLTF_ERR_PARSE, "root.asset must be present and an object", "root.asset", 1, 1);
  }

  yyjson_val *version_val = yyjson_obj_get(asset_val, "version");
  if (!version_val || !yyjson_is_str(version_val)) {
    fail(GLTF_ERR_PARSE, "root.asset.version must be present and a string", "root.asset.version", 1, 1);
  }

  const char *version = yyjson_get_str(version_val);
  if (strlen(version) >= sizeof(doc->asset_version)) {
    fail(GLTF_ERR_PARSE, "root.asset.version is too long", "root.asset.version", 1, 1);
  }
  (void)strncpy(doc->asset_version, version, sizeof(doc->asset_version) - 1);
  doc->asset_version[sizeof(doc->asset_version) - 1] = '\0';

  yyjson_val *generator_val = yyjson_obj_get(asset_val, "generator");
  if (generator_val && yyjson_is_str(generator_val)) {
    const char *generator = yyjson_get_str(generator_val);
    doc->asset_generator = arena_strdup(&doc->arena, generator);
    if (!gltf_str_is_valid(doc->asset_generator)) {
      fail(GLTF_ERR_IO, "out of memory", "root.asset.generator", 1, 1);
    }
  }

  yyjson_doc_free(json_doc);
  json_doc = NULL;

  *out_doc = doc;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;

#undef fail
}

void gltf_free(gltf_doc* doc) {
  if (doc) {
    free(doc->scenes);
    free(doc->nodes);
    free(doc->meshes);
    free(doc->indices_u32);
    free(doc->arena.data);
    free(doc);
  }
}

uint32_t gltf_doc_scene_count(const gltf_doc* doc) {
  return doc ? doc->scene_count : 0u;
}

uint32_t gltf_doc_node_count(const gltf_doc* doc) {
  return doc ? doc->node_count : 0u;
}

uint32_t gltf_doc_mesh_count(const gltf_doc* doc) {
  return doc ? doc->mesh_count : 0u;
}

const char* gltf_doc_asset_version(const gltf_doc* doc) {
  return doc ? doc->asset_version : NULL;
}

const char* gltf_doc_asset_generator(const gltf_doc* doc) {
  if (!doc) return NULL;
  return arena_get_str(&doc->arena, doc->asset_generator);
}

int32_t gltf_doc_default_scene(const gltf_doc* doc) {
  if (!doc) return GLTF_DOC_DEFAULT_SCENE_INVALID;
  return doc->default_scene >= 0 ? (int32_t)doc->default_scene : GLTF_DOC_DEFAULT_SCENE_INVALID;
}

const char* gltf_doc_scene_name(const gltf_doc* doc, uint32_t scene_index) {
  if (!doc) return NULL;
  if (scene_index >= doc->scene_count) return NULL;
  return arena_get_str(&doc->arena, doc->scenes[scene_index].name);
}

uint32_t gltf_doc_scene_node_count(const gltf_doc* doc, uint32_t scene_index) {
  if (!doc) return 0u;
  if (scene_index >= doc->scene_count) return 0u;
  return doc->scenes[scene_index].nodes.count;
}

int gltf_doc_scene_node(const gltf_doc* doc, uint32_t scene_index, uint32_t i, uint32_t* out_node_index) {
  if (!doc) return 0;
  if (scene_index >= doc->scene_count) return 0;

  const gltf_scene* scene = &doc->scenes[scene_index];
  
  if (i >= scene->nodes.count) return 0;
  if (!out_node_index) return 0;
  
  *out_node_index = doc->indices_u32[scene->nodes.first + i];
  
  return 1;
}

const char* gltf_doc_node_name(const gltf_doc* doc, uint32_t node_index) {
  if (!doc) return NULL;
  if (node_index >= doc->node_count) return NULL;
  return arena_get_str(&doc->arena, doc->nodes[node_index].name);
}

int32_t gltf_doc_node_mesh(const gltf_doc* doc, uint32_t node_index) {
  if (!doc) return -1;
  if (node_index >= doc->node_count) return -1;
  return doc->nodes[node_index].mesh;
}

uint32_t gltf_doc_node_child_count(const gltf_doc* doc, uint32_t node_index) {
  if (!doc) return 0u;
  if (node_index >= doc->node_count) return 0u;
  return doc->nodes[node_index].children.count;
}

int gltf_doc_node_child(const gltf_doc* doc, uint32_t node_index, uint32_t i, uint32_t* out_child_index) {
  if (!doc) return 0;
  if (node_index >= doc->node_count) return 0;
  
  const gltf_node* node = &doc->nodes[node_index];
  
  if (i >= node->children.count) return 0;
  if (!out_child_index) return 0;

  *out_child_index = doc->indices_u32[node->children.first + i];
  return 1;
}

const char* gltf_doc_mesh_name(const gltf_doc* doc, uint32_t mesh_index) {
  if (!doc) return NULL;
  if (mesh_index >= doc->mesh_count) return NULL;
  return arena_get_str(&doc->arena, doc->meshes[mesh_index].name);
}
