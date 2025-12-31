// gltf.c - Minimal Educational glTF 2.0 loader (implementation)
//
// This file contains the implementation behind the public API declared in
// gltf/gltf.h. The public API contract is documented in the header; comments
// here focus on internal representation and helper behavior.

// ----------------------------------------------------------------------------
// Includes
// ----------------------------------------------------------------------------

#include "gltf/gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <yyjson.h>

// ----------------------------------------------------------------------------
// Forward Declarations - Internal Helpers
// ----------------------------------------------------------------------------

// Base64 decoding (src/base64.c - not part of public API)
size_t gltf_base64_max_decoded_size(size_t in_len);

int gltf_base64_decode(const char* in,
                       size_t in_len,
                       uint8_t* out,
                       size_t out_cap,
                       size_t* out_len);

// File system utilities (src/fs.c - not part of public API)
typedef enum gltf_fs_status {
  GLTF_FS_OK = 0,
  GLTF_FS_INVALID,
  GLTF_FS_IO,
  GLTF_FS_OOM,
  GLTF_FS_SIZE_MISMATCH,
  GLTF_FS_TOO_LARGE
} gltf_fs_status;

size_t gltf_fs_dir_len(const char* path);

char* gltf_fs_join_dir_leaf(const char* dir_prefix,
                            size_t dir_len,
                            const char* leaf);

gltf_fs_status gltf_fs_read_file_exact_u32(const char* path,
                                           uint32_t expected_len,
                                           uint8_t** out_data,
                                           uint32_t* out_len);

// ----------------------------------------------------------------------------
// Constants - Internal configuration and sentinel values
// ----------------------------------------------------------------------------

#define GLTF_STR_INVALID_OFF 0xFFFFFFFFu
#define GLTF_ARENA_INITIAL_CAPACITY (16 * 1024)
#define GLTF_INDICES_INITIAL_CAP 256u
#define GLTF_DOC_DEFAULT_SCENE_INVALID -1

// ----------------------------------------------------------------------------
// Internal Type Definitions
// ----------------------------------------------------------------------------

// Half-open range into doc->indices_u32: [first, first + count).
typedef struct gltf_range_u32 {
  uint32_t first;
  uint32_t count;
} gltf_range_u32;

// Growable byte pool for all document strings (UTF-8, NUL-terminated).
typedef struct gltf_arena {
  uint8_t* data;
  size_t size; // bytes used
  size_t cap;  // bytes allocated
} gltf_arena;

// String reference into doc->arena.
// Invalid strings use off == GLTF_STR_INVALID_OFF.
typedef struct gltf_str {
  uint32_t off; // byte offset in arena->data
  uint32_t len; // not including '\0'
} gltf_str;

// Parsed glTF scene (name + root nodes).
typedef struct gltf_scene {
  gltf_str name;        // optional scene name
  gltf_range_u32 nodes; // root node indices (variable length, into doc->indices_u32)
} gltf_scene;

// Parsed glTF node (name + optional mesh + child nodes).
typedef struct gltf_node {
  gltf_str name;           // optional node name
  int32_t mesh;            // mesh index, or -1 if not present
  gltf_range_u32 children; // child node indices (variable length, into doc->indices_u32)
} gltf_node;

// Parsed glTF primitive.
typedef struct gltf_primitive {
  uint32_t position_accessor; // accessor index for POSITION attribute (required)
  int32_t indices_accessor;   // accessor index for indices, or -1 if non-indexed
} gltf_primitive;

// Parsed glTF mesh (name + range into doc->primitives).
typedef struct gltf_mesh {
  gltf_str name;            // optional mesh name
  uint32_t primitive_first; // first primitive index in doc->primitives
  uint32_t primitive_count; // number of primitives in this mesh
} gltf_mesh;

// Parsed glTF accessor (typed view into bufferView data).
typedef struct gltf_accessor {
  int32_t buffer_view;     // index into doc->buffer_views, or -1 if absent
  uint32_t byte_offset;    // offset within the buffer view (default 0)
  uint32_t component_type; // component encoding (5120=int8 to 5126=float32)
  uint32_t count;          // number of elements
  uint8_t type;            // element type: SCALAR, VEC2, VEC3, VEC4, MAT2, MAT3, MAT4
  uint8_t normalized;      // 0/1: whether integer components should be normalized
  uint16_t _pad;           // padding for alignment
} gltf_accessor;

// Parsed glTF bufferView (slice of a buffer + optional stride).
typedef struct gltf_buffer_view {
  uint32_t buffer;      // index into doc->buffers (which buffer this view slices)
  uint32_t byte_offset; // offset into the buffer (default 0)
  uint32_t byte_length; // size of this view in bytes
  uint32_t byte_stride; // stride between elements (0 = tightly packed)
  uint32_t target;      // GPU binding hint (GL_ARRAY_BUFFER,
                        // GL_ELEMENT_ARRAY_BUFFER, etc.)
} gltf_buffer_view;

// Parsed glTF buffer (URI + loaded bytes).
typedef struct gltf_buffer {
  gltf_str uri;         // source: file path or data URI
  uint32_t byte_length; // size of loaded data in bytes
  uint8_t* data;        // loaded bytes (owned by doc), size == byte_length
} gltf_buffer;

// Internal document layout (opaque to users).
//
// Notes:
//   - All memory is owned by the document and freed in gltf_free().
//   - Relationships are expressed via indices:
//       scenes -> nodes -> meshes -> primitives -> accessors -> bufferViews -> buffers
struct gltf_doc {
  // asset.version is small; store inline for quick access and no arena lookup.
  char asset_version[8];

  // Optional asset.generator string stored in arena (invalid if absent).
  gltf_str asset_generator;

  // Default scene index from top-level "scene" property,
  // or GLTF_DOC_DEFAULT_SCENE_INVALID if not specified.
  int32_t default_scene;

  // Top-level array counts (0 if absent).
  uint32_t scene_count;
  uint32_t node_count;
  uint32_t mesh_count;
  uint32_t primitive_count;
  uint32_t buffer_count;
  uint32_t buffer_view_count;
  uint32_t accessor_count;

  // Parsed arrays (owned).
  //
  // All arrays are flat and indexed by integer handles.
  // Relationships between objects are expressed via indices or index ranges.
  gltf_scene* scenes;             // [scene_count]
  gltf_node* nodes;               // [node_count]
  gltf_mesh* meshes;              // [mesh_count]
  gltf_primitive* primitives;     // [primitive_count]
  gltf_buffer* buffers;           // [buffer_count]
  gltf_buffer_view* buffer_views; // [buffer_view_count]
  gltf_accessor* accessors;       // [accessor_count]

  // Shared index pool for all variable-length arrays (owned).
  //
  // Stored as a single growable array to avoid per-object allocations.
  // Referenced via gltf_range_u32 (half-open ranges).
  //
  // Currently used by:
  // - scenes[i].nodes       (root node indices)
  // - nodes[i].children    (child node indices)
  //
  // Future uses may include:
  // - skin joints
  // - animation channel targets
  uint32_t* indices_u32;
  uint32_t indices_count; // number of valid entries
  uint32_t indices_cap;   // allocated entries

  // Arena storage for all strings (owned).
  //
  // Stores UTF-8 NUL-terminated strings:
  // - names
  // - URIs
  // - generator
  gltf_arena arena;
};

// ----------------------------------------------------------------------------
// Internal Helper Functions
// ----------------------------------------------------------------------------

// String reference helpers

static inline gltf_str gltf_str_invalid(void) {
  gltf_str s = { GLTF_STR_INVALID_OFF, 0 };
  return s;
}

static inline int gltf_str_is_valid(gltf_str s) {
  return s.off != GLTF_STR_INVALID_OFF;
}

// Error reporting (null-safe).

static void
gltf_set_err(gltf_error* out_err,
             const char* message,
             const char* path,
             int line, int col) {
  if (!out_err) return;
  out_err->message = message;
  out_err->path = path;
  out_err->line = line;
  out_err->col = col;
}

// String arena.

static void arena_init(gltf_arena* arena, size_t initial_cap) {
  if (initial_cap == 0) initial_cap = GLTF_ARENA_INITIAL_CAPACITY;
  arena->data = (uint8_t*)malloc(initial_cap);
  arena->size = 0;
  arena->cap = arena->data ? initial_cap : 0;
}

static int arena_reserve(gltf_arena* arena, size_t additional_bytes) {
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

static gltf_str arena_strdup(gltf_arena* arena, const char* s) {
  if (!s) return gltf_str_invalid();

  size_t len = strlen(s);
  
  if (arena_reserve(arena, len + 1) == 0) return gltf_str_invalid();
  
  uint32_t off = (uint32_t)arena->size;
  memcpy(&arena->data[arena->size], s, len + 1);
  arena->size += len + 1;
  gltf_str result = { off, (uint32_t)len };
  
  return result;
}

static const char* arena_get_str(const gltf_arena* arena, gltf_str s) {
  if (!gltf_str_is_valid(s)) return NULL;
  if ((size_t)s.off + (size_t)s.len + 1 > arena->size) return NULL;
  return (const char*)(arena->data + s.off);
}

// Shared index pool for variable-length u32 index lists.

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

// ----------------------------------------------------------------------------
// JSON Parsing Helpers
// ----------------------------------------------------------------------------
//
// Small helpers to read/validate JSON fields and report errors consistently.

// Optional scalar helpers

static gltf_result gltf_json_get_u32(yyjson_val* obj,
                                     const char* key,
                                     uint32_t default_value,
                                     uint32_t* out,
                                     const char* err_path,
                                     gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }
  if (!yyjson_is_uint(v)) {
    gltf_set_err(out_err, "must be an unsigned integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint64_t x = yyjson_get_uint(v);
  if (x > UINT32_MAX) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (uint32_t)x;
  return GLTF_OK;
}

static gltf_result gltf_json_get_i32(yyjson_val* obj,
                                     const char* key,
                                     int32_t default_value,
                                     int32_t* out,
                                     const char* err_path,
                                     gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }
  if (!yyjson_is_int(v)) {
    gltf_set_err(out_err, "must be an integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  int64_t x = yyjson_get_int(v);
  if (x > INT32_MAX || x < INT32_MIN) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (int32_t)x;
  return GLTF_OK;
}

static gltf_result gltf_json_get_bool(yyjson_val* obj,
                                      const char* key,
                                      int default_value,
                                      uint8_t* out,
                                      const char* err_path,
                                      gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value ? 1 : 0;
    return GLTF_OK;
  }

  if (!yyjson_is_bool(v)) {
    gltf_set_err(out_err, "must be boolean", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = yyjson_get_bool(v) ? 1u : 0u;
  return GLTF_OK;
}

static gltf_result gltf_json_get_str_opt_dup_arena(yyjson_val* obj,
                                                   const char* key,
                                                   gltf_arena* arena,
                                                   gltf_str* out,
                                                   const char* err_path,
                                                   gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = gltf_str_invalid();
    return GLTF_OK;
  }
  if (!yyjson_is_str(v)) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* s = yyjson_get_str(v);
  // yyjson string should not be NULL if yyjson_is_str(v)
  if (!s) {
    *out = gltf_str_invalid();
    return GLTF_OK;
  }

  gltf_str gs = arena_strdup(arena, s);
  if (!gltf_str_is_valid(gs)) {
    gltf_set_err(out_err, "out of memory", err_path, 1, 1);
    return GLTF_ERR_IO;
  }

  *out = gs;
  return GLTF_OK;
}

// Required scalar helpers

static gltf_result gltf_json_get_u32_req(yyjson_val* obj,
                                         const char* key,
                                         uint32_t* out,
                                         const char* err_path,
                                         gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    gltf_set_err(out_err, "must be present", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!yyjson_is_uint(v)) {
    gltf_set_err(out_err, "must be an unsigned integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint64_t x = yyjson_get_uint(v);
  if (x > UINT32_MAX) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (uint32_t)x;
  return GLTF_OK;
}

static gltf_result gltf_json_get_accessor_type_required(yyjson_val* obj,
                                                        const char* key,
                                                        uint8_t* out,
                                                        const char* err_path,
                                                        gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    gltf_set_err(out_err, "must be present", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!yyjson_is_str(v)) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* s = yyjson_get_str(v);
  if (!s) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (strcmp(s, "SCALAR") == 0) {
    *out = GLTF_ACCESSOR_SCALAR;
  } else if (strcmp(s, "VEC2") == 0) {
    *out = GLTF_ACCESSOR_VEC2;
  } else if (strcmp(s, "VEC3") == 0) {
    *out = GLTF_ACCESSOR_VEC3;
  } else if (strcmp(s, "VEC4") == 0) {
    *out = GLTF_ACCESSOR_VEC4;
  } else if (strcmp(s, "MAT4") == 0) {
    *out = GLTF_ACCESSOR_MAT4;
  } else {
    gltf_set_err(out_err, "invalid accessor type", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  return GLTF_OK;
}

// Array range helpers

static gltf_result gltf_json_get_u32_index_array_range_opt(gltf_doc* doc,
                                                           yyjson_val* obj,
                                                           const char* key,
                                                           gltf_range_u32* out_range,
                                                           const char* err_path_arr,
                                                           const char* err_path_elem,
                                                           gltf_error* out_err) {
  if (!doc || !out_range) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    out_range->first = 0;
    out_range->count = 0;
    return GLTF_OK;
  }

  if (!yyjson_is_arr(v)) {
    gltf_set_err(out_err, "must be array", err_path_arr, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t first = doc->indices_count;

  size_t idx, nmax;
  yyjson_val* it = NULL;
  yyjson_arr_foreach(v, idx, nmax, it) {
    if (!yyjson_is_uint(it)) {
      gltf_set_err(out_err, "must be unsigned integer", err_path_elem, 1, 1);
      return GLTF_ERR_PARSE;
    }

    uint64_t x = yyjson_get_uint(it);
    if (x > UINT32_MAX) {
      gltf_set_err(out_err, "integer out of range", err_path_elem, 1, 1);
      return GLTF_ERR_PARSE;
    }

    if (!indices_push_u32(doc, (uint32_t)x)) {
      gltf_set_err(out_err, "out of memory", err_path_elem, 1, 1);
      return GLTF_ERR_IO;
    }
  }

  out_range->first = first;
  out_range->count = (uint32_t)(doc->indices_count - first);
  return GLTF_OK;
}

static gltf_result gltf_decode_data_uri(const char* uri,
                                        uint32_t expected_len,
                                        uint8_t** out_bytes,
                                        uint32_t* out_len,
                                        gltf_error* out_err) {
  if (!uri || !out_bytes || !out_len) {
    gltf_set_err(out_err, "invalid arguments", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_INVALID;
  }

  *out_bytes = NULL;
  *out_len = 0;

  if (strncmp(uri, "data:", 5) != 0) {
    gltf_set_err(out_err, "buffer uri must start with 'data:'", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* tag = ";base64,";
  const char* p = strstr(uri, tag);
  if (!p) {
    gltf_set_err(out_err, "only base64 data URIs are supported", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* payload = p + strlen(tag);
  const size_t payload_len = strlen(payload);

  // Decide allocation size.
  size_t cap = 0;
  if (expected_len != 0) {
    cap = (size_t)expected_len;
  } else {
    cap = gltf_base64_max_decoded_size(payload_len);
    if (cap == SIZE_MAX) {
      gltf_set_err(out_err, "data uri payload too large", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_PARSE;
    }
  }

  uint8_t* bytes = NULL;
  if (cap > 0) {
    bytes = (uint8_t*)malloc(cap);
    if (!bytes) {
      gltf_set_err(out_err, "out of memory allocating decoded buffer", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_IO;
    }
  }

  size_t decoded_len = 0;
  int ok = gltf_base64_decode(payload, payload_len, bytes, cap, &decoded_len);
  if (!ok) {
    free(bytes);
    gltf_set_err(out_err, "invalid base64 payload", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (decoded_len > UINT32_MAX) {
    free(bytes);
    gltf_set_err(out_err, "decoded buffer too large", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (expected_len != 0 && decoded_len != (size_t)expected_len) {
    free(bytes);
    gltf_set_err(out_err,
                 "decoded buffer length does not match byteLength",
                 "root.buffers[].byteLength",
                 1,
                 1);
    return GLTF_ERR_PARSE;
  }

  *out_bytes = bytes;
  *out_len = (uint32_t)decoded_len;
  return GLTF_OK;
}

// ----------------------------------------------------------------------------
// Document Lifetime Management
// ----------------------------------------------------------------------------

gltf_result gltf_load_file(const char* path, gltf_doc** out_doc, gltf_error* out_err) {
#define GLTF_FAIL(_r, ...)               \
  do {                                   \
    gltf_set_err(out_err, __VA_ARGS__);  \
    yyjson_doc_free(json_doc);           \
    gltf_free(doc);                      \
    return _r;                           \
  } while (0)

#define GLTF_TRY(expr)                   \
  do {                                   \
    gltf_result _r = (expr);             \
    if (_r != GLTF_OK) {                 \
      yyjson_doc_free(json_doc);         \
      gltf_free(doc);                    \
      return _r;                         \
    }                                    \
  } while (0)

  gltf_doc* doc = NULL;
  yyjson_read_err err;
  yyjson_doc* json_doc = NULL;

  if (out_doc) {
    *out_doc = NULL;
  }

  if (!path || !out_doc) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  json_doc = yyjson_read_file(path, 0, NULL, &err);
  if (!json_doc) {
    GLTF_FAIL(GLTF_ERR_PARSE, err.msg, "root", 1, 1);
  }

  doc = (gltf_doc*)calloc(1, sizeof(gltf_doc));
  if (!doc) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  arena_init(&doc->arena, GLTF_ARENA_INITIAL_CAPACITY);
  if (!doc->arena.data) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  // Root
  yyjson_val* root = yyjson_doc_get_root(json_doc);
  if (!root || !yyjson_is_obj(root)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root", 1, 1);
  }

  // Default scene
  GLTF_TRY(gltf_json_get_i32(root,
                             "scene",
                             GLTF_DOC_DEFAULT_SCENE_INVALID,
                             &doc->default_scene,
                             "root.scene",
                             out_err));

  // Scenes
  yyjson_val* scenes_val = yyjson_obj_get(root, "scenes");
  if (!scenes_val) {
    doc->scene_count = 0;
  } else if (yyjson_is_arr(scenes_val)) {
    doc->scene_count = (unsigned)yyjson_arr_size(scenes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.scenes", 1, 1);
  }

  if (doc->scene_count > 0) {
    doc->scenes = (gltf_scene*)calloc(doc->scene_count, sizeof(gltf_scene));
    if (!doc->scenes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.scenes", 1, 1);
    }

    size_t scene_idx, scene_max;
    yyjson_val* scene_val = NULL;
    yyjson_arr_foreach(scenes_val, scene_idx, scene_max, scene_val) {
      if (!yyjson_is_obj(scene_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.scenes[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_str_opt_dup_arena(scene_val,
                                               "name",
                                               &doc->arena,
                                               &doc->scenes[scene_idx].name,
                                               "root.scenes[].name",
                                               out_err));

      GLTF_TRY(gltf_json_get_u32_index_array_range_opt(doc,
                                                       scene_val,
                                                       "nodes",
                                                       &doc->scenes[scene_idx].nodes,
                                                       "root.scenes[].nodes",
                                                       "root.scenes[].nodes[]",
                                                       out_err));
    }
  }

  // Nodes
  yyjson_val* nodes_val = yyjson_obj_get(root, "nodes");
  if (!nodes_val) {
    doc->node_count = 0;
  } else if (yyjson_is_arr(nodes_val)) {
    doc->node_count = (unsigned)yyjson_arr_size(nodes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.nodes", 1, 1);
  }

  if (doc->node_count > 0) {
    doc->nodes = (gltf_node*)calloc(doc->node_count, sizeof(gltf_node));
    if (!doc->nodes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.nodes", 1, 1);
    }

    size_t node_idx, node_max;
    yyjson_val* node_val = NULL;
    yyjson_arr_foreach(nodes_val, node_idx, node_max, node_val) {
      if (!yyjson_is_obj(node_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.nodes[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_str_opt_dup_arena(node_val,
                                               "name",
                                               &doc->arena,
                                               &doc->nodes[node_idx].name,
                                               "root.nodes[].name",
                                               out_err));

      GLTF_TRY(gltf_json_get_i32(node_val,
                                 "mesh",
                                 -1,
                                 &doc->nodes[node_idx].mesh,
                                 "root.nodes[].mesh",
                                 out_err));
    }
  }

  // Meshes
  yyjson_val* meshes_val = yyjson_obj_get(root, "meshes");
  if (!meshes_val) {
    doc->mesh_count = 0;
  } else if (yyjson_is_arr(meshes_val)) {
    doc->mesh_count = (unsigned)yyjson_arr_size(meshes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.meshes", 1, 1);
  }

  if (doc->mesh_count > 0) {
    doc->meshes = (gltf_mesh*)calloc(doc->mesh_count, sizeof(gltf_mesh));
    if (!doc->meshes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.meshes", 1, 1);
    }

    size_t mesh_idx, mesh_max;
    yyjson_val* mesh_val = NULL;
    yyjson_arr_foreach(meshes_val, mesh_idx, mesh_max, mesh_val) {
      if (!yyjson_is_obj(mesh_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_str_opt_dup_arena(mesh_val,
                                               "name",
                                               &doc->arena,
                                               &doc->meshes[mesh_idx].name,
                                               "root.meshes[].name",
                                               out_err));

      uint32_t primitive_first = doc->primitive_count;
      uint32_t primitive_count = 0;

      yyjson_val* primitives_val = yyjson_obj_get(mesh_val, "primitives");
      if (!primitives_val) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.meshes[].primitives", 1, 1);
      } else if (!yyjson_is_arr(primitives_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.meshes[].primitives", 1, 1);
      }

      primitive_count = (unsigned)yyjson_arr_size(primitives_val);

      if (primitive_count > 0) {
        size_t new_count = (size_t)doc->primitive_count + (size_t)primitive_count;

        if (new_count > SIZE_MAX / sizeof(gltf_primitive)) {
          GLTF_FAIL(GLTF_ERR_IO, "too many primitives", "root.meshes[].primitives", 1, 1);
        }

        gltf_primitive* primitives = (gltf_primitive*)realloc(doc->primitives, new_count * sizeof(gltf_primitive));
        if (!primitives) {
          GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.meshes[].primitives", 1, 1);
        }
        memset(&primitives[doc->primitive_count], 0, primitive_count * sizeof(gltf_primitive));
        doc->primitives = primitives;

        size_t prim_idx, prim_max;
        yyjson_val* prim_val = NULL;
        yyjson_arr_foreach(primitives_val, prim_idx, prim_max, prim_val) {
          if (!yyjson_is_obj(prim_val)) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[].primitives[]", 1, 1);
          }

          yyjson_val* attributes_val = yyjson_obj_get(prim_val, "attributes");
          if (!attributes_val) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.meshes[].primitives[].attributes", 1, 1);
          }
          if (!yyjson_is_obj(attributes_val)) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[].primitives[].attributes", 1, 1);
          }

          uint32_t primitive_idx = primitive_first + prim_idx;

          // POSITION is treated as optional for loading; queries will error if missing.
          GLTF_TRY(gltf_json_get_u32(attributes_val, "POSITION", 0xFFFFFFFFu,
                                     &doc->primitives[primitive_idx].position_accessor,
                                     "root.meshes[].primitives[].attributes.POSITION",
                                     out_err));

          GLTF_TRY(gltf_json_get_i32(prim_val, "indices", -1,
                                     &doc->primitives[primitive_idx].indices_accessor,
                                     "root.meshes[].primitives[].indices",
                                     out_err));
        }
      }

      doc->meshes[mesh_idx].primitive_first = primitive_first;
      doc->meshes[mesh_idx].primitive_count = primitive_count;
      doc->primitive_count += primitive_count;
    }
  }

  // Accessors
  yyjson_val* accessors_val = yyjson_obj_get(root, "accessors");
  if (!accessors_val) {
    doc->accessor_count = 0;
  } else if (yyjson_is_arr(accessors_val)) {
    doc->accessor_count = (unsigned)yyjson_arr_size(accessors_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.accessors", 1, 1);
  }

  if (doc->accessor_count > 0) {
    doc->accessors = (gltf_accessor*)calloc(doc->accessor_count, sizeof(gltf_accessor));
    if (!doc->accessors) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.accessors", 1, 1);
    }

    size_t accessor_idx, accessor_max;
    yyjson_val* accessor_val = NULL;
    yyjson_arr_foreach(accessors_val, accessor_idx, accessor_max, accessor_val) {
      if (!yyjson_is_obj(accessor_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.accessors[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_i32(accessor_val,
                                 "bufferView",
                                 -1,
                                 &doc->accessors[accessor_idx].buffer_view,
                                 "root.accessors[].bufferView",
                                 out_err));

      if (doc->accessors[accessor_idx].buffer_view < 0) {
        // This property MUST NOT be defined when bufferView is undefined.
        GLTF_TRY(gltf_json_get_u32(accessor_val,
                                   "byteOffset",
                                   0,
                                   &doc->accessors[accessor_idx].byte_offset,
                                   "root.accessors[].byteOffset",
                                   out_err));
      }

      GLTF_TRY(gltf_json_get_u32_req(accessor_val,
                                     "componentType",
                                     &doc->accessors[accessor_idx].component_type,
                                     "root.accessors[].componentType",
                                     out_err));

      GLTF_TRY(gltf_json_get_u32_req(accessor_val,
                                     "count",
                                     &doc->accessors[accessor_idx].count,
                                     "root.accessors[].count",
                                     out_err));

      GLTF_TRY(gltf_json_get_accessor_type_required(accessor_val,
                                                    "type",
                                                    &doc->accessors[accessor_idx].type,
                                                    "root.accessors[].type",
                                                    out_err));

      GLTF_TRY(gltf_json_get_bool(accessor_val,
                                  "normalized",
                                  0,
                                  &doc->accessors[accessor_idx].normalized,
                                  "root.accessors[].normalized",
                                  out_err));
    }
  }

  // Buffer Views
  yyjson_val* buffer_views_val = yyjson_obj_get(root, "bufferViews");
  if (!buffer_views_val) {
    doc->buffer_view_count = 0;
  } else if (yyjson_is_arr(buffer_views_val)) {
    doc->buffer_view_count = (unsigned)yyjson_arr_size(buffer_views_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.bufferViews", 1, 1);
  }

  if (doc->buffer_view_count > 0) {
    doc->buffer_views = (gltf_buffer_view*)calloc(doc->buffer_view_count, sizeof(gltf_buffer_view));
    if (!doc->buffer_views) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.bufferViews", 1, 1);
    }

    size_t buffer_view_idx, buffer_view_max;
    yyjson_val* buffer_view_val = NULL;
    yyjson_arr_foreach(buffer_views_val, buffer_view_idx, buffer_view_max, buffer_view_val) {
      if (!yyjson_is_obj(buffer_view_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.bufferViews[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_u32_req(buffer_view_val,
                                     "buffer",
                                     &doc->buffer_views[buffer_view_idx].buffer,
                                     "root.bufferViews[].buffer",
                                     out_err));

      GLTF_TRY(gltf_json_get_u32(buffer_view_val,
                                 "byteLength",
                                 0,
                                 &doc->buffer_views[buffer_view_idx].byte_length,
                                 "root.bufferViews[].byteLength",
                                 out_err));

      GLTF_TRY(gltf_json_get_u32_req(buffer_view_val,
                                     "byteOffset",
                                     &doc->buffer_views[buffer_view_idx].byte_offset,
                                     "root.bufferViews[].byteOffset",
                                     out_err));

      GLTF_TRY(gltf_json_get_u32(buffer_view_val,
                                 "byteStride",
                                 0,
                                 &doc->buffer_views[buffer_view_idx].byte_stride,
                                 "root.bufferViews[].byteStride",
                                 out_err));

      GLTF_TRY(gltf_json_get_u32(buffer_view_val,
                                 "target",
                                 0,
                                 &doc->buffer_views[buffer_view_idx].target,
                                 "root.bufferViews[].target",
                                 out_err));
    }
  }

  // Buffers
  yyjson_val* buffers_val = yyjson_obj_get(root, "buffers");
  if (!buffers_val) {
    doc->buffer_count = 0;
  } else if (yyjson_is_arr(buffers_val)) {
    doc->buffer_count = (unsigned)yyjson_arr_size(buffers_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.buffers", 1, 1);
  }

  if (doc->buffer_count > 0) {
    doc->buffers = (gltf_buffer*)calloc(doc->buffer_count, sizeof(gltf_buffer));
    if (!doc->buffers) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers", 1, 1);
    }

    size_t buffer_idx, buffer_max;
    yyjson_val* buffer_val = NULL;
    yyjson_arr_foreach(buffers_val, buffer_idx, buffer_max, buffer_val) {
      if (!yyjson_is_obj(buffer_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.buffers[]", 1, 1);
      }

      GLTF_TRY(gltf_json_get_u32_req(buffer_val,
                                     "byteLength",
                                     &doc->buffers[buffer_idx].byte_length,
                                     "root.buffers[].byteLength",
                                     out_err));

      yyjson_val* uri_val = yyjson_obj_get(buffer_val, "uri");
      if (!uri_val) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.buffers[].uri", 1, 1);
      } else if (!yyjson_is_str(uri_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be string", "root.buffers[].uri", 1, 1);
      } else {
        const char* uri = yyjson_get_str(uri_val);
        gltf_str v = arena_strdup(&doc->arena, uri);
        if (!gltf_str_is_valid(v)) {
          GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].uri", 1, 1);
        }
        doc->buffers[buffer_idx].uri = v;

        if (strncmp(uri, "data:", 5) != 0) {
          // External file

          size_t dir_len = gltf_fs_dir_len(path);
          char* full = gltf_fs_join_dir_leaf(path, dir_len, uri);
          if (!full) {
            GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].uri", 1, 1);
          }

          uint32_t actual_len = 0;
          uint8_t* data = NULL;
          gltf_fs_status st = gltf_fs_read_file_exact_u32(full,
                                                          doc->buffers[buffer_idx].byte_length,
                                                          &data,
                                                          &actual_len);
          free(full);

          switch (st) {
          case GLTF_FS_OK:
            doc->buffers[buffer_idx].data = data;
            break;
          case GLTF_FS_SIZE_MISMATCH:
            GLTF_FAIL(GLTF_ERR_PARSE,
                      "buffer file size does not match byteLength",
                      "root.buffers[].byteLength",
                      1,
                      1);
            break;
          case GLTF_FS_OOM:
            GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].byteLength", 1, 1);
            break;
          case GLTF_FS_TOO_LARGE:
            GLTF_FAIL(GLTF_ERR_PARSE, "buffer file too large", "root.buffers[].uri", 1, 1);
            break;
          default:
            GLTF_FAIL(GLTF_ERR_IO, "failed to read buffer file", "root.buffers[].uri", 1, 1);
            break;
          }
        } else {
          // Data URI (base64)

          uint8_t* bytes = NULL;
          uint32_t out_len = 0;
          GLTF_TRY(gltf_decode_data_uri(uri,
                                        doc->buffers[buffer_idx].byte_length,
                                        &bytes,
                                        &out_len,
                                        out_err));
          doc->buffers[buffer_idx].data = bytes;
        }
      }
    }
  }

  yyjson_val* asset_val = yyjson_obj_get(root, "asset");
  if (!asset_val || !yyjson_is_obj(asset_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and an object", "root.asset", 1, 1);
  }

  yyjson_val* version_val = yyjson_obj_get(asset_val, "version");
  if (!version_val || !yyjson_is_str(version_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and a string", "root.asset.version", 1, 1);
  }

  const char* version = yyjson_get_str(version_val);
  if (strlen(version) >= sizeof(doc->asset_version)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "too long", "root.asset.version", 1, 1);
  }
  (void)strncpy(doc->asset_version, version, sizeof(doc->asset_version) - 1);
  doc->asset_version[sizeof(doc->asset_version) - 1] = '\0';

  yyjson_val* generator_val = yyjson_obj_get(asset_val, "generator");
  if (generator_val && yyjson_is_str(generator_val)) {
    const char* generator = yyjson_get_str(generator_val);
    doc->asset_generator = arena_strdup(&doc->arena, generator);
    if (!gltf_str_is_valid(doc->asset_generator)) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.asset.generator", 1, 1);
    }
  }

  yyjson_doc_free(json_doc);
  json_doc = NULL;

  *out_doc = doc;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;

#undef GLTF_TRY
#undef GLTF_FAIL
}

void gltf_free(gltf_doc* doc) {
  if (doc) {
    free(doc->scenes);
    free(doc->nodes);
    free(doc->meshes);
    free(doc->primitives);
    if (doc->buffers) {
      for (uint32_t i = 0; i < doc->buffer_count; i++) {
        free(doc->buffers[i].data);
      }
    }
    free(doc->buffers);
    free(doc->buffer_views);
    free(doc->accessors);
    free(doc->indices_u32);
    free(doc->arena.data);
    free(doc);
  }
}

// ----------------------------------------------------------------------------
// Public Document Query API
// ----------------------------------------------------------------------------

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

// Scene queries

uint32_t gltf_doc_scene_count(const gltf_doc* doc) {
  return doc ? doc->scene_count : 0u;
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

int gltf_doc_scene_node(const gltf_doc* doc,
                        uint32_t scene_index,
                        uint32_t i,
                        uint32_t* out_node_index) {
  if (!doc) return 0;
  if (scene_index >= doc->scene_count) return 0;

  const gltf_scene* scene = &doc->scenes[scene_index];

  if (i >= scene->nodes.count) return 0;
  if (!out_node_index) return 0;

  *out_node_index = doc->indices_u32[scene->nodes.first + i];

  return 1;
}

// Node queries

uint32_t gltf_doc_node_count(const gltf_doc* doc) {
  return doc ? doc->node_count : 0u;
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

int gltf_doc_node_child(const gltf_doc* doc,
                        uint32_t node_index,
                        uint32_t i,
                        uint32_t* out_child_index) {
  if (!doc) return 0;
  if (node_index >= doc->node_count) return 0;

  const gltf_node* node = &doc->nodes[node_index];

  if (i >= node->children.count) return 0;
  if (!out_child_index) return 0;

  *out_child_index = doc->indices_u32[node->children.first + i];
  return 1;
}

// Mesh queries

uint32_t gltf_doc_mesh_count(const gltf_doc* doc) {
  return doc ? doc->mesh_count : 0u;
}

const char* gltf_doc_mesh_name(const gltf_doc* doc, uint32_t mesh_index) {
  if (!doc) return NULL;
  if (mesh_index >= doc->mesh_count) return NULL;
  return arena_get_str(&doc->arena, doc->meshes[mesh_index].name);
}

// ----------------------------------------------------------------------------
// Binary Data Decoding Helpers
// ----------------------------------------------------------------------------

// Endian utilities (little-endian, unaligned).

static uint16_t rd_u16_le(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

static uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

// Component metadata.

static int gltf_accessor_component_count(uint32_t accessor_type, uint32_t* out_count) {
  if (!out_count) return 0;
  switch (accessor_type) {
  case GLTF_ACCESSOR_SCALAR: *out_count = 1; break;
  case GLTF_ACCESSOR_VEC2:   *out_count = 2; break;
  case GLTF_ACCESSOR_VEC3:   *out_count = 3; break;
  case GLTF_ACCESSOR_VEC4:   *out_count = 4; break;
  case GLTF_ACCESSOR_MAT2:   *out_count = 4; break;
  case GLTF_ACCESSOR_MAT3:   *out_count = 9; break;
  case GLTF_ACCESSOR_MAT4:   *out_count = 16; break;
  default: return 0;
  }
  return 1;
}

static int gltf_component_size_bytes(uint32_t component_type, uint32_t* out_size) {
  if (!out_size) return 0;
  switch (component_type) {
  case GLTF_COMP_I8:
  case GLTF_COMP_U8:  *out_size = 1; break;
  case GLTF_COMP_I16:
  case GLTF_COMP_U16: *out_size = 2; break;
  case GLTF_COMP_U32:
  case GLTF_COMP_F32: *out_size = 4; break;
  default: return 0;
  }
  return 1;
}

// Component decoding.
// Converts exactly one component to float (optionally normalized).

static gltf_result gltf_decode_component_to_f32(const uint8_t* p,
                                                uint32_t component_type,
                                                int normalized,
                                                float* out) {
  if (!p || !out) return GLTF_ERR_INVALID;

  switch (component_type) {
  case GLTF_COMP_F32: {
    uint32_t u = rd_u32_le(p);
    float f;
    memcpy(&f, &u, sizeof f);
    *out = f;
    return GLTF_OK;
  }

  case GLTF_COMP_U8: {
    uint32_t v = (uint32_t)p[0];
    *out = normalized ? (float)v / 255.0f : (float)v;
    return GLTF_OK;
  }

  case GLTF_COMP_I8: {
    int8_t v = (int8_t)p[0];
    if (normalized) {
      *out = (v == (int8_t)-128) ? -1.0f : (float)v / 127.0f;
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  case GLTF_COMP_U16: {
    uint32_t v = (uint32_t)rd_u16_le(p);
    *out = normalized ? (float)v / 65535.0f : (float)v;
    return GLTF_OK;
  }

  case GLTF_COMP_I16: {
    int16_t v = (int16_t)rd_u16_le(p);
    if (normalized) {
      *out = (v == (int16_t)-32768) ? -1.0f : (float)v / 32767.0f;
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  case GLTF_COMP_U32: {
    uint32_t v = rd_u32_le(p);
    if (normalized) {
      *out = (float)((double)v / 4294967295.0);
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  default:
    return GLTF_ERR_PARSE;
  }
}

// ----------------------------------------------------------------------------
// Mesh Primitive Geometry Access API
// ----------------------------------------------------------------------------

uint32_t gltf_doc_mesh_primitive_count(const gltf_doc* doc, uint32_t mesh_index) {
  if (!doc) return 0u;
  if (mesh_index >= doc->mesh_count) return 0u;
  return doc->meshes[mesh_index].primitive_count;
}

int gltf_doc_mesh_primitive(const gltf_doc* doc,
                            uint32_t mesh_index,
                            uint32_t prim_i,
                            uint32_t* out_primitive_index) {
  if (!doc) return 0;
  if (mesh_index >= doc->mesh_count) return 0;

  const gltf_mesh* mesh = &doc->meshes[mesh_index];

  if (prim_i >= mesh->primitive_count) return 0;
  if (!out_primitive_index) return 0;

  *out_primitive_index = mesh->primitive_first + prim_i;

  return 1;
}

int gltf_mesh_primitive_get_accessors(const gltf_doc* doc,
                                      uint32_t mesh_index,
                                      uint32_t prim_i,
                                      uint32_t* out_position_accessor,
                                      int32_t* out_indices_accessor) {
  if (!doc) return 0;
  if (mesh_index >= doc->mesh_count) return 0;

  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  
  if (prim_i >= mesh->primitive_count) return 0;
  
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  
  if (!out_position_accessor || !out_indices_accessor) return 0;
  
  *out_position_accessor = prim->position_accessor;
  *out_indices_accessor = prim->indices_accessor;
  
  return 1;
}

gltf_result gltf_mesh_primitive_position_span(const gltf_doc* doc,
                                              uint32_t mesh_index,
                                              uint32_t prim_i,
                                              gltf_span* out_span,
                                              gltf_error* out_err) {
  if (!doc || !out_span) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  return gltf_accessor_span(doc, prim->position_accessor, out_span, out_err);
}

gltf_result gltf_mesh_primitive_read_position_f32(const gltf_doc* doc,
                                                  uint32_t mesh_index,
                                                  uint32_t prim_i,
                                                  uint32_t vertex_i,
                                                  float out_xyz[3],
                                                  gltf_error* out_err) {
  if (!doc || !out_xyz) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  gltf_span span;

  gltf_result r = gltf_accessor_span(doc, prim->position_accessor, &span, out_err);
  if (r != GLTF_OK) {
    gltf_set_err(out_err, "failed to get position accessor span", "root.accessors[]", 1, 1);
    return r;
  }

  const gltf_accessor* a = &doc->accessors[prim->position_accessor];
  if (vertex_i >= a->count) {
    gltf_set_err(out_err, "vertex index out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  uint32_t comp_count = 0;
  if (!gltf_accessor_component_count(a->type, &comp_count)) {
    gltf_set_err(out_err, "invalid accessor type", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (comp_count < 3) {
    gltf_set_err(out_err,
                 "position accessor has less than 3 components",
                 "root.accessors[].type",
                 1,
                 1);
    return GLTF_ERR_PARSE;
  }
  uint32_t comp_size = 0;
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }
  const uint32_t elem_size = comp_count * comp_size;
  const uint32_t stride = (span.stride != 0) ? span.stride : elem_size;
  const uint8_t* vertex_ptr = (const uint8_t*)span.ptr + (size_t)vertex_i * (size_t)stride;
  for (uint32_t c = 0; c < 3; c++) {
    gltf_result r = gltf_decode_component_to_f32(vertex_ptr + (size_t)c * (size_t)comp_size,
                                                 a->component_type,
                                                 a->normalized,
                                                 &out_xyz[c]);

    if (r != GLTF_OK) {
      gltf_set_err(out_err, "failed to get position accessor span", "root.accessors[]", 1, 1);
      return r;
    }
  }
  return GLTF_OK;
}

int gltf_mesh_primitive_has_indices(const gltf_doc* doc, uint32_t mesh_index, uint32_t prim_i) {
  if (!doc) return 0;
  if (mesh_index >= doc->mesh_count) return 0;
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) return 0;
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  return prim->indices_accessor >= 0 ? 1 : 0;
}

gltf_result gltf_mesh_primitive_index_count(const gltf_doc* doc,
                                            uint32_t mesh_index,
                                            uint32_t prim_i,
                                            uint32_t* out_count,
                                            gltf_error* out_err) {

  if (!doc || !out_count) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  if (prim->indices_accessor < 0) {
    gltf_set_err(out_err, "primitive has no indices", "root.meshes[].primitives[].indices", 1, 1);
    return GLTF_ERR_PARSE;
  }
  const gltf_accessor* a = &doc->accessors[(uint32_t)prim->indices_accessor];
  *out_count = a->count;
  return GLTF_OK;
}

gltf_result gltf_mesh_primitive_read_index_u32(const gltf_doc* doc,
                                               uint32_t mesh_index,
                                               uint32_t prim_i,
                                               uint32_t index_i,
                                               uint32_t* out_index,
                                               gltf_error* out_err) {
  if (!doc || !out_index) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  if (prim->indices_accessor < 0) {
    gltf_set_err(out_err, "primitive has no indices", "root.meshes[].primitives[].indices", 1, 1);
    return GLTF_ERR_PARSE;
  }
  gltf_span span;
  gltf_result r = gltf_accessor_span(doc, (uint32_t)prim->indices_accessor, &span, out_err);
  if (r != GLTF_OK) {
    gltf_set_err(out_err, "failed to get indices accessor span", "root.accessors[]", 1, 1);
    return r;
  }
  const gltf_accessor* a = &doc->accessors[(uint32_t)prim->indices_accessor];
  if (index_i >= a->count) {
    gltf_set_err(out_err, "index out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  uint32_t comp_size = 0;
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }
  const uint32_t elem_size = comp_size;
  const uint32_t stride = (span.stride != 0) ? span.stride : elem_size;
  const uint8_t* index_ptr = (const uint8_t*)span.ptr + (size_t)index_i * (size_t)stride;
  switch (a->component_type) {
  case GLTF_COMP_U8:
    *out_index = (uint32_t)index_ptr[0];
    return GLTF_OK;
  case GLTF_COMP_U16:
    *out_index = (uint32_t)rd_u16_le(index_ptr);
    return GLTF_OK;
  case GLTF_COMP_U32:
    *out_index = rd_u32_le(index_ptr);
    return GLTF_OK;
  default:
    gltf_set_err(out_err, "invalid index componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }
}

gltf_result gltf_mesh_primitive_indices_span(const gltf_doc* doc,
                                             uint32_t mesh_index,
                                             uint32_t prim_i,
                                             gltf_span* out_span,
                                             gltf_error* out_err) {
  if (!doc || !out_span) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];
  if (prim->indices_accessor < 0) {
    gltf_set_err(out_err, "primitive has no indices", "root.meshes[].primitives[].indices", 1, 1);
    return GLTF_ERR_PARSE;
  }
  return gltf_accessor_span(doc, (uint32_t)prim->indices_accessor, out_span, out_err);
}

gltf_result gltf_mesh_primitive_view(const gltf_doc* doc,
                                     uint32_t mesh_index,
                                     uint32_t prim_i,
                                     gltf_draw_primitive_view* out_view,
                                     gltf_error* out_err) {
  if (!doc || !out_view) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (mesh_index >= doc->mesh_count) {
    gltf_set_err(out_err, "mesh out of range", "root.meshes[]", 1, 1);
    return GLTF_ERR_INVALID;
  }
  const gltf_mesh* mesh = &doc->meshes[mesh_index];
  if (prim_i >= mesh->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.meshes[].primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  const gltf_primitive* prim = &doc->primitives[mesh->primitive_first + prim_i];

  if (prim->indices_accessor >= 0) {
    const gltf_accessor* a = &doc->accessors[(uint32_t)prim->indices_accessor];
    out_view->index_count = a->count;
  } else {
    out_view->index_count = 0;
  }
  return GLTF_OK;
}

// ----------------------------------------------------------------------------
// Accessor Data Access API
// ----------------------------------------------------------------------------

uint32_t gltf_doc_accessor_count(const gltf_doc* doc) {
  return doc ? doc->accessor_count : 0u;
}

int gltf_doc_accessor_info(const gltf_doc* doc,
                           uint32_t accessor_index,
                           uint32_t* out_count,
                           uint32_t* out_component_type,
                           uint32_t* out_type,
                           int* out_normalized) {
  if (!doc) return 0;
  if (accessor_index >= doc->accessor_count) return 0;
  if (!out_count || !out_component_type || !out_type || !out_normalized) return 0;

  const gltf_accessor* a = &doc->accessors[accessor_index];
  *out_count = a->count;
  *out_component_type = a->component_type;
  *out_type = a->type;
  *out_normalized = a->normalized ? 1 : 0;
  return 1;
}

gltf_result gltf_accessor_span(const gltf_doc* doc,
                               uint32_t accessor_index,
                               gltf_span* out_span,
                               gltf_error* out_err) {
  if (!doc || !out_span) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (accessor_index >= doc->accessor_count) {
    gltf_set_err(out_err, "accessor out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  const gltf_accessor* a = &doc->accessors[accessor_index];
  if (a->buffer_view < 0) {
    gltf_set_err(out_err, "accessor has no bufferView", "root.accessors[].bufferView", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if ((uint32_t)a->buffer_view >= doc->buffer_view_count) {
    gltf_set_err(out_err, "bufferView out of range", "root.accessors[].bufferView", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const gltf_buffer_view* bv = &doc->buffer_views[(uint32_t)a->buffer_view];

  if (bv->buffer >= doc->buffer_count) {
    gltf_set_err(out_err, "buffer out of range", "root.bufferViews[].buffer", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const gltf_buffer* b = &doc->buffers[bv->buffer];
  if (b->byte_length > 0 && !b->data) {
    gltf_set_err(out_err, "buffer data not loaded", "root.buffers[]", 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t comp_count = 0;
  uint32_t comp_size = 0;
  if (!gltf_accessor_component_count(a->type, &comp_count)) {
    gltf_set_err(out_err, "invalid accessor type", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (comp_count > UINT32_MAX / comp_size) {
    gltf_set_err(out_err, "accessor element size overflow", "root.accessors[]", 1, 1);
    return GLTF_ERR_PARSE;
  }
  const uint32_t elem_size = comp_count * comp_size;
  const uint32_t stride = (bv->byte_stride != 0) ? bv->byte_stride : elem_size;
  if (stride < elem_size) {
    gltf_set_err(out_err,
                 "bufferView.byteStride smaller than element size",
                 "root.bufferViews[].byteStride",
                 1,
                 1);
    return GLTF_ERR_PARSE;
  }
  if ((size_t)bv->byte_offset > SIZE_MAX - (size_t)a->byte_offset) {
    gltf_set_err(out_err, "accessor offset overflow", "root.accessors[]", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const size_t base = (size_t)bv->byte_offset + (size_t)a->byte_offset;
  const size_t count = (size_t)a->count;

  // base is absolute (buffer space). Need relative offset inside the view:
  const size_t rel = (size_t)a->byte_offset; // relative to bufferView

  if (rel > (size_t)bv->byte_length) {
    gltf_set_err(out_err,
                 "accessor offset out of bufferView bounds",
                 "root.accessors[].byteOffset",
                 1,
                 1);
    return GLTF_ERR_PARSE;
  }

  if (count > 0) {
    const size_t last_rel = rel + (count - 1u) * (size_t)stride;
    if (last_rel > SIZE_MAX - (size_t)elem_size) {
      gltf_set_err(out_err, "accessor range overflow", "root.accessors[]", 1, 1);
      return GLTF_ERR_PARSE;
    }
    const size_t end_rel = last_rel + (size_t)elem_size;
    if (end_rel > (size_t)bv->byte_length) {
      gltf_set_err(out_err, "accessor range out of bufferView bounds", "root.accessors[]", 1, 1);
      return GLTF_ERR_PARSE;
    }
  }

  gltf_span sp;
  sp.ptr = (b->data && a->count > 0) ? (const uint8_t*)(b->data + base) : NULL;
  sp.count = a->count;
  sp.stride = stride;
  sp.elem_size = elem_size;
  *out_span = sp;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;
}

gltf_result gltf_accessor_read_f32(const gltf_doc* doc,
                                   uint32_t accessor_index,
                                   uint32_t i,
                                   float* out,
                                   uint32_t out_cap,
                                   gltf_error* out_err) {
  if (!doc || !out) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (accessor_index >= doc->accessor_count) {
    gltf_set_err(out_err, "accessor out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const gltf_accessor* a = &doc->accessors[accessor_index];
  uint32_t comp_count = 0;
  if (!gltf_accessor_component_count(a->type, &comp_count)) {
    gltf_set_err(out_err, "invalid accessor type", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (out_cap < comp_count) {
    gltf_set_err(out_err, "output buffer too small", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  gltf_span sp;
  gltf_result r = gltf_accessor_span(doc, accessor_index, &sp, out_err);
  if (r != GLTF_OK) {
    return r;
  }
  if (!sp.ptr && sp.count > 0) {
    gltf_set_err(out_err, "span has no data", "root", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (i >= sp.count) {
    gltf_set_err(out_err, "element index out of range", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  uint32_t comp_size = 0;
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const uint8_t* p = sp.ptr + (size_t)i * (size_t)sp.stride;
  for (uint32_t k = 0; k < comp_count; k++) {
    float v = 0.0f;
    r = gltf_decode_component_to_f32(p + (size_t)k * (size_t)comp_size,
                                     a->component_type,
                                     a->normalized ? 1 : 0,
                                     &v);
    if (r != GLTF_OK) {
      gltf_set_err(out_err, "failed to decode component", "root.accessors[]", 1, 1);
      return r;
    }
    out[k] = v;
  }

  return GLTF_OK;
}

// ----------------------------------------------------------------------------
// Utility and Math Helpers
// ----------------------------------------------------------------------------

gltf_result gltf_compute_aabb_pos3_f32_span(const gltf_doc* doc,
                                            uint32_t accessor_index,
                                            float out_min3[3],
                                            float out_max3[3],
                                            gltf_error* out_err) {
  if (!doc || !out_min3 || !out_max3) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  gltf_span span;
  gltf_result r = gltf_accessor_span(doc, accessor_index, &span, out_err);
  if (r != GLTF_OK) {
    return r;
  }
  if (!span.ptr && span.count > 0) {
    gltf_set_err(out_err, "span has no data", "root", 1, 1);
    return GLTF_ERR_PARSE;
  }
  const gltf_accessor* a = &doc->accessors[accessor_index];
  uint32_t comp_count = 0;
  if (!gltf_accessor_component_count(a->type, &comp_count)) {
    gltf_set_err(out_err, "invalid accessor type", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (comp_count < 3) {
    gltf_set_err(out_err, "accessor has less than 3 components", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  uint32_t comp_size = 0;
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }
  // Initialize bounds to first element.
  {
    const uint8_t* p = span.ptr;
    for (uint32_t c = 0; c < 3; c++) {
      float v = 0.0f;
      r = gltf_decode_component_to_f32(p + (size_t)c * (size_t)comp_size,
                                       a->component_type,
                                       a->normalized ? 1 : 0,
                                       &v);
      if (r != GLTF_OK) {
        gltf_set_err(out_err, "failed to decode component", "root.accessors[]", 1, 1);
        return r;
      }
      out_min3[c] = v;
      out_max3[c] = v;
    }
  }
  // Process remaining elements.
  for (uint32_t i = 1; i < span.count; i++) {
    const uint8_t* p = span.ptr + (size_t)i * (size_t)span.stride;
    for (uint32_t c = 0; c < 3; c++) {
      float v = 0.0f;
      r = gltf_decode_component_to_f32(p + (size_t)c * (size_t)comp_size,
                                       a->component_type,
                                       a->normalized ? 1 : 0,
                                       &v);
      if (r != GLTF_OK) {
        gltf_set_err(out_err, "failed to decode component", "root.accessors[]", 1, 1);
        return r;
      }
      if (v < out_min3[c]) out_min3[c] = v;
      if (v > out_max3[c]) out_max3[c] = v;
    }
  }
  return GLTF_OK;
}
