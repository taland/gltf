// Minimal educational glTF 2.0 loader (C11).
//
// Internal header shared across implementation files.
//
// Notes:
//   - Public API is declared in include/gltf/gltf.h.
//   - Keep comments here short; avoid duplicating public contracts.


#include "gltf/gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yyjson.h>

#define GLTF_STR_INVALID_OFF 0xFFFFFFFFu
#define GLTF_ARENA_INITIAL_CAPACITY (16 * 1024)
#define GLTF_INDICES_INITIAL_CAP 256u
#define GLTF_DOC_DEFAULT_SCENE_INVALID -1


// Flags that tweak load behavior for specific container types.
typedef enum gltf_load_ctx_flags {
  GLTF_LOAD_CTX_NONE = 0,
  GLTF_LOAD_CTX_GLB  = 1 << 0,  // forbid external uri, allow internal_bin
} gltf_load_ctx_flags;

// Optional context passed to loaders (bin override, doc dir, flags).
typedef struct gltf_load_context {
  // If non-NULL, buffers[0] without uri resolves to this memory (GLB BIN chunk).
  const uint8_t* internal_bin;
  uint32_t internal_bin_size;

  // If non-NULL, used to resolve external relative uris (for .gltf).
  const char* doc_dir;

  // Optional behavior flags
  uint32_t flags;
} gltf_load_context;

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

// Parsed glTF node (name + optional mesh + child nodes + local transform).
typedef struct gltf_node {
  gltf_str name;           // optional node name
  int32_t mesh;            // mesh index, or -1 if not present
  gltf_range_u32 children; // child node indices (variable length, into doc->indices_u32)

  // Transform: either matrix, or TRS.
  int has_matrix;
  float matrix[16];     // column-major

  float translation[3]; // default (0,0,0)
  float rotation[4];    // default (0,0,0,1)  // x,y,z,w
  float scale[3];       // default (1,1,1)
} gltf_node;

// Parsed glTF primitive.
typedef struct gltf_primitive {
  uint32_t attributes_first;
  uint32_t attributes_count;
  int32_t indices_accessor;   // accessor index for indices, or -1 if non-indexed
  gltf_prim_mode mode;        // default TRIANGLES (4)
} gltf_primitive;

// Parsed glTF primitive attribute (semantic + set index + accessor).
typedef struct gltf_prim_attr {
  gltf_attr_semantic semantic;   // POSITION/NORMAL/...
  uint32_t set_index;            // for TEXCOORD_n/COLOR_n (0/1/2...), else 0
  uint32_t accessor_index;       // index into doc->accessors[]
} gltf_prim_attr;

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
//       materials -> textures -> images (+ optional samplers)
struct gltf_doc {
  // asset.version is small; store inline for quick access and no arena lookup.
  char asset_version[8];

  // Optional asset.generator string stored in arena (invalid if absent).
  gltf_str asset_generator;

  // Directory of the loaded .gltf file (UTF-8, NUL-terminated, stored in arena).
  // Used to resolve relative image URIs (images[i].uri -> images[i].resolved).
  // Invalid if the document was not loaded from a filesystem path.
  gltf_str doc_dir;

  // Default scene index from top-level "scene" property,
  // or GLTF_DOC_DEFAULT_SCENE_INVALID if not specified.
  int32_t default_scene;

  // Top-level array counts (0 if absent).
  uint32_t scene_count;
  uint32_t node_count;
  uint32_t mesh_count;
  uint32_t primitive_count;
  uint32_t prim_attr_count;
  uint32_t buffer_count;
  uint32_t buffer_view_count;
  uint32_t accessor_count;

  // Material / texture arrays (0 if absent).
  uint32_t material_count;
  uint32_t texture_count;
  uint32_t image_count;
  uint32_t sampler_count;

  // Parsed arrays (owned).
  //
  // All arrays are flat and indexed by integer handles.
  // Relationships between objects are expressed via indices or index ranges.
  gltf_scene* scenes;             // [scene_count]
  gltf_node* nodes;               // [node_count]
  gltf_mesh* meshes;              // [mesh_count]
  gltf_primitive* primitives;     // [primitive_count]
  gltf_prim_attr* prim_attrs;     // [prim_attr_count]
  gltf_buffer* buffers;           // [buffer_count]
  gltf_buffer_view* buffer_views; // [buffer_view_count]
  gltf_accessor* accessors;       // [accessor_count]

  // Materials / textures (owned).
  gltf_material* materials;       // [material_count]
  gltf_texture* textures;         // [texture_count]
  gltf_image* images;             // [image_count]
  gltf_sampler* samplers;         // [sampler_count]

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
  // - URIs (raw)
  // - resolved filesystem paths
  // - generator
  // - doc_dir
  gltf_arena arena;
};

// ----------------------------------------------------------------------------
// String references (header-only)
// ----------------------------------------------------------------------------

static inline gltf_str gltf_str_invalid(void) {
  gltf_str s = { GLTF_STR_INVALID_OFF, 0 };
  return s;
}

static inline int gltf_str_is_valid(gltf_str s) {
  return s.off != GLTF_STR_INVALID_OFF;
}


// ----------------------------------------------------------------------------
// Error reporting (src/gltf_doc.c)
// ----------------------------------------------------------------------------


void gltf_set_err(gltf_error* out_err,
                  const char* message,
                  const char* path,
                  int line,
                  int col);

void gltf_set_err_if(gltf_error* out_err,
                     const char* message,
                     const char* path,
                     int line, int col);


// ----------------------------------------------------------------------------
// Arena (strings) (src/gltf_memory.c)
// ----------------------------------------------------------------------------

void arena_init(gltf_arena* arena, size_t initial_cap);

int arena_reserve(gltf_arena* arena, size_t additional_bytes);

gltf_str arena_strdup(gltf_arena* arena, const char* s);

const char* arena_get_str(const gltf_arena* arena, gltf_str s);

// ----------------------------------------------------------------------------
// Indices (shared u32 pool) (src/gltf_memory.c)
// ----------------------------------------------------------------------------

int indices_reserve(gltf_doc* doc, uint32_t additional);

int indices_push_u32(gltf_doc* doc, uint32_t v);

// ----------------------------------------------------------------------------
// Component metadata (src/gltf_decode.c)
// ----------------------------------------------------------------------------

int gltf_accessor_component_count(uint32_t accessor_type, uint32_t* out_count);

int gltf_component_size_bytes(uint32_t component_type, uint32_t* out_size);

gltf_result gltf_decode_component_to_f32(const uint8_t* p,
                                         uint32_t component_type,
                                         int normalized,
                                         float* out);

// ----------------------------------------------------------------------------
// Optional scalars (src/gltf_parse.c)
// ----------------------------------------------------------------------------

gltf_result gltf_json_get_u32(yyjson_val* obj,
                              const char* key,
                              uint32_t default_value,
                              uint32_t* out,
                              const char* err_path,
                              gltf_error* out_err);

gltf_result gltf_json_get_i32(yyjson_val* obj,
                              const char* key,
                              int32_t default_value,
                              int32_t* out,
                              const char* err_path,
                              gltf_error* out_err);

gltf_result gltf_json_get_f32(yyjson_val* obj,
                              const char* key,
                              float default_value,
                              float* out,
                              const char* err_path,
                              gltf_error* out_err);

gltf_result gltf_json_get_bool(yyjson_val* obj,
                               const char* key,
                               int default_value,
                               gltf_bool* out,
                               const char* err_path,
                               gltf_error* out_err);

gltf_result gltf_json_get_bool_u8(yyjson_val* obj,
                                  const char* key,
                                  int default_value,
                                  uint8_t* out,
                                  const char* err_path,
                                  gltf_error* out_err);

gltf_result gltf_json_get_material_alpha_mode(yyjson_val* obj,
                                              const char* key,
                                              gltf_alpha_mode default_value,
                                              gltf_alpha_mode* out,
                                              const char* err_path,
                                              gltf_error* out_err);

gltf_result gltf_json_get_str_opt_dup_arena(yyjson_val* obj,
                                            const char* key,
                                            gltf_arena* arena,
                                            gltf_str* out,
                                            const char* err_path,
                                            gltf_error* out_err);

gltf_result gltf_json_get_str_opt_dup_arena_cstr(yyjson_val* obj,
                                                 const char* key,
                                                 gltf_arena* arena,
                                                 const char** out,
                                                 const char* err_path,
                                                 gltf_error* out_err);

gltf_attr_semantic gltf_parse_semantic(const char* key, uint32_t* out_set_index);


// ----------------------------------------------------------------------------
// Required scalars (src/gltf_parse.c)
// ----------------------------------------------------------------------------


gltf_result gltf_json_get_vec3_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[3],
                                       float out[3],
                                       const char* err_path,
                                       gltf_error* out_err);

gltf_result gltf_json_get_vec4_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[4],
                                       float out[4],
                                       const char* err_path,
                                       gltf_error* out_err);

gltf_result gltf_json_get_mat4_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[16],
                                       float out[16],
                                       const char* err_path,
                                       gltf_error* out_err);

gltf_result gltf_json_get_u32_req(yyjson_val* obj,
                                  const char* key,
                                  uint32_t* out,
                                  const char* err_path,
                                  gltf_error* out_err);

gltf_result gltf_json_get_accessor_type_required(yyjson_val* obj,
                                                 const char* key,
                                                 uint8_t* out,
                                                 const char* err_path,
                                                 gltf_error* out_err);


// ----------------------------------------------------------------------------
// Arrays (index lists) (src/gltf_parse.c)
// ----------------------------------------------------------------------------


gltf_result gltf_json_get_u32_index_array_range_opt(gltf_doc* doc,
                                                    yyjson_val* obj,
                                                    const char* key,
                                                    gltf_range_u32* out_range,
                                                    const char* err_path_arr,
                                                    const char* err_path_elem,
                                                    gltf_error* out_err);

// Parsing utilities for gltf_doc.c

gltf_result gltf_parse_scenes(gltf_doc* doc,
                    yyjson_val* root,
                              gltf_error* out_err);

gltf_result gltf_parse_nodes(gltf_doc* doc,
                    yyjson_val* root,
                             gltf_error* out_err);

gltf_result gltf_parse_meshes(gltf_doc* doc,
                    yyjson_val* root,
                              gltf_error* out_err);

gltf_result gltf_parse_accessors(gltf_doc* doc,
                      yyjson_val* root,
                                 gltf_error* out_err);

gltf_result gltf_parse_buffer_views(gltf_doc* doc,
                        yyjson_val* root,
                                    gltf_error* out_err);

gltf_result gltf_parse_buffers(gltf_doc* doc,
                               yyjson_val* root,
                               const gltf_load_context* ctx,
                               gltf_error* out_err);

gltf_result gltf_parse_images(gltf_doc* doc,
                              yyjson_val* root,
                              gltf_error* out_err);

gltf_result gltf_parse_samplers(gltf_doc* doc,
                                yyjson_val* root,
                                gltf_error* out_err);

gltf_result gltf_parse_textures(gltf_doc* doc,
                                yyjson_val* root,
                                gltf_error* out_err);

gltf_result gltf_parse_materials(gltf_doc* doc,
                                 yyjson_val* root,
                                 gltf_error* out_err);

gltf_result gltf_decode_data_uri(const char* uri,
                                 uint32_t expected_len,
                                 uint8_t** out_bytes,
                                 uint32_t* out_len,
                                 gltf_error* out_err);

// ----------------------------------------------------------------------------
// Little-endian reads from an unaligned byte stream (src/gltf_decode.c).
// ----------------------------------------------------------------------------

uint16_t rd_u16_le(const uint8_t* p);

uint32_t rd_u32_le(const uint8_t* p);


// ----------------------------------------------------------------------------
// Scene graph evaluation (internal)
// ----------------------------------------------------------------------------

// Cache for computed node world matrices for a single scene.
//
// Lifetime:
//   - created by gltf_world_cache_create()
//   - reused across gltf_compute_world_matrices() calls
//   - freed by gltf_world_cache_free()
//
// Meaning:
//   - world_m16[node*16+0 .. node*16+15] holds node world matrix (column-major)
//   - state[node] tracks DFS progress:
//       0 = UNVISITED, 1 = VISITING, 2 = DONE
//
// Notes:
//   - Valid only after gltf_compute_world_matrices() succeeds.
//   - scene_index tells which scene the cache currently represents.
//   - A node may remain UNVISITED if it is unreachable from scene roots.

typedef struct gltf_world_cache {
  uint32_t node_count;  // must match doc->node_count
  float* world_m16;     // node_count * 16 floats, column-major mat4
  uint8_t* state;       // node_count bytes, DFS state per node
  uint32_t scene_index; // scene for which matrices were computed
  int valid;            // 1 if world_m16/state are consistent for scene_index
} gltf_world_cache;
