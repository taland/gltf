#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gltf_bool {
  GLTF_FALSE = 0,
  GLTF_TRUE  = 1
} gltf_bool;

// Opaque document handle.
// All memory is owned by the document and freed via gltf_free().
typedef struct gltf_doc gltf_doc;

// Error details filled by gltf_load_file() on failure.
typedef struct gltf_error {
  const char* message; // static literal, may be NULL if out_err was NULL
  const char* path;    // static literal (e.g. "root.scenes"), may be NULL
  int line;
  int col;
} gltf_error;

// Result codes returned by public API.
typedef enum gltf_result {
  GLTF_OK = 0,
  GLTF_ERR_IO,           // file I/O or allocation failure
  GLTF_ERR_PARSE,        // invalid JSON or wrong types/structure
  GLTF_ERR_RANGE,        // requested index out of range
  GLTF_ERR_INVALID,      // invalid arguments passed by the caller
  GLTF_ERR_UNSUPPORTED,  // feature not compiled in (e.g. images)
} gltf_result;

// Span-like view of accessor data.
//
// Represents a safe, bounds-checked window over typed data stored in a buffer.
// The span does NOT own the memory; all memory is owned by the document.
//
// Layout:
//   element i starts at: ptr + i * stride
//   packed element size (without stride padding): elem_size
typedef struct gltf_span {
  const uint8_t* ptr;   // Pointer to the first element (element 0)
  uint32_t count;       // Number of elements in the accessor
  uint32_t stride;      // Byte stride between consecutive elements
  uint32_t elem_size;   // Packed size of a single element (in bytes)
} gltf_span;

// glTF accessor componentType values (glTF 2.0 specification).
typedef enum gltf_component_type {
  GLTF_COMP_I8   = 5120, // BYTE
  GLTF_COMP_U8   = 5121, // UNSIGNED_BYTE
  GLTF_COMP_I16  = 5122, // SHORT
  GLTF_COMP_U16  = 5123, // UNSIGNED_SHORT
  GLTF_COMP_U32  = 5125, // UNSIGNED_INT
  GLTF_COMP_F32  = 5126, // FLOAT
} gltf_component_type;

// glTF accessor type values (number of components per element).
typedef enum gltf_accessor_type {
  GLTF_ACCESSOR_SCALAR = 1, // 1 component
  GLTF_ACCESSOR_VEC2,       // 2 components
  GLTF_ACCESSOR_VEC3,       // 3 components
  GLTF_ACCESSOR_VEC4,       // 4 components
  GLTF_ACCESSOR_MAT2,       // 4 components (column-major)
  GLTF_ACCESSOR_MAT3,       // 9 components (column-major)
  GLTF_ACCESSOR_MAT4,       // 16 components (column-major)
} gltf_accessor_type;

// glTF primitive modes (drawing modes).
typedef enum gltf_prim_mode {
  GLTF_PRIM_POINTS         = 0,
  GLTF_PRIM_LINES          = 1,
  GLTF_PRIM_LINE_LOOP      = 2,
  GLTF_PRIM_LINE_STRIP     = 3,
  GLTF_PRIM_TRIANGLES      = 4,
  GLTF_PRIM_TRIANGLE_STRIP = 5,
  GLTF_PRIM_TRIANGLE_FAN   = 6,
} gltf_prim_mode;

// Attribute semantics for primitive attributes.
typedef enum gltf_attr_semantic {
  GLTF_ATTR_UNKNOWN = 0,

  GLTF_ATTR_POSITION,
  GLTF_ATTR_NORMAL,
  GLTF_ATTR_TANGENT,

  GLTF_ATTR_TEXCOORD,   // TEXCOORD_n
  GLTF_ATTR_COLOR,      // COLOR_n

  GLTF_ATTR_JOINTS,     // JOINTS_n
  GLTF_ATTR_WEIGHTS,    // WEIGHTS_n
} gltf_attr_semantic;


// Loads a glTF 2.0 JSON (.gltf) file.
//
// Success:
//   - returns GLTF_OK
//   - *out_doc is set to a valid document handle
//
// Failure:
//   - returns a non-OK code
//   - *out_doc is set to NULL
//   - out_err (if non-NULL) is filled with error context (see gltf_error)
gltf_result gltf_load_file(const char* path, gltf_doc** out_doc, gltf_error* out_err);

// Frees all memory owned by the document.
// Safe to call with NULL.
void gltf_free(gltf_doc* doc);


// ----------------------------------------------------------------------------
// Scenes / Nodes / Meshes (basic)
// ----------------------------------------------------------------------------

// Returns the number of scenes in the document.
// Returns 0 if doc is NULL.
uint32_t gltf_doc_scene_count(const gltf_doc* doc);

// Returns the number of nodes in the document.
// Returns 0 if doc is NULL.
uint32_t gltf_doc_node_count(const gltf_doc* doc);

// Returns the number of meshes in the document.
// Returns 0 if doc is NULL.
uint32_t gltf_doc_mesh_count(const gltf_doc* doc);

// Returns the asset version string (e.g. "2.0").
// The returned pointer is owned by the document and valid until gltf_free().
// Returns NULL if doc is NULL.
const char* gltf_doc_asset_version(const gltf_doc* doc);

// Returns the asset generator string if present.
// The returned pointer is owned by the document and valid until gltf_free().
// Returns NULL if generator is absent or if doc is NULL.
const char* gltf_doc_asset_generator(const gltf_doc* doc);

// Returns the default scene index from the top-level "scene" property.
// Returns -1 if not provided or if doc is NULL.
int32_t gltf_doc_default_scene(const gltf_doc* doc);

// Returns the scene name, or NULL if the scene is unnamed.
// Returns NULL if doc is NULL or scene_index is out of range.
const char* gltf_doc_scene_name(const gltf_doc* doc, uint32_t scene_index);

// Returns the number of root nodes referenced by the scene (scene.nodes).
// Returns 0 if doc is NULL or scene_index is out of range.
uint32_t gltf_doc_scene_node_count(const gltf_doc* doc, uint32_t scene_index);

// Returns the i-th root node index referenced by the scene.
//
// On success:
//   - returns 1
//   - writes the node index to *out_node_index
//
// On failure (doc NULL, out_node_index NULL, out of range):
//   - returns 0
//   - *out_node_index is not modified
int gltf_doc_scene_node(const gltf_doc* doc,
                        uint32_t scene_index,
                        uint32_t i,
                        uint32_t* out_node_index);

// Returns the node name, or NULL if the node is unnamed.
// Returns NULL if doc is NULL or node_index is out of range.
const char* gltf_doc_node_name(const gltf_doc* doc, uint32_t node_index);

// Returns the mesh index referenced by the node.
// Returns -1 if the node has no mesh or if doc is NULL / node_index is out of range.
int32_t gltf_doc_node_mesh(const gltf_doc* doc, uint32_t node_index);

// Returns the number of children of the node (node.children).
// Returns 0 if doc is NULL or node_index is out of range.
uint32_t gltf_doc_node_child_count(const gltf_doc* doc, uint32_t node_index);

// Returns the i-th child node index.
//
// On success:
//   - returns 1
//   - writes the child index to *out_child_index
//
// On failure (doc NULL, out_child_index NULL, out of range):
//   - returns 0
//   - *out_child_index is not modified
int gltf_doc_node_child(const gltf_doc* doc,
                        uint32_t node_index,
                        uint32_t i,
                        uint32_t* out_child_index);

// Returns the mesh name, or NULL if the mesh is unnamed.
// Returns NULL if doc is NULL or mesh_index is out of range.
const char* gltf_doc_mesh_name(const gltf_doc* doc, uint32_t mesh_index);


// ----------------------------------------------------------------------------
// Mesh primitives
// ----------------------------------------------------------------------------
//
// Notes:
//   - A mesh can have multiple primitives (each primitive is a draw call).
//   - We expose only:
//       * POSITION accessor (required by our loader contract)
//       * indices accessor (optional)
//
// Coordinate space:
//   - Accessor POSITION data is stored in mesh-local coordinates.
//   - Nodes (and scenes) later provide transforms to place the mesh in world space.

// Returns the number of primitives referenced by the mesh.
// Returns 0 if doc is NULL or mesh_index is out of range.
uint32_t gltf_doc_mesh_primitive_count(const gltf_doc* doc, uint32_t mesh_index);

// Returns the primitive index (into the document primitive array) for mesh primitive prim_i.
//
// On success:
//   - returns 1
//   - writes the primitive index to *out_primitive_index
//
// On failure (doc NULL, out_primitive_index NULL, out of range):
//   - returns 0
//   - *out_primitive_index is not modified
int gltf_doc_mesh_primitive(const gltf_doc* doc,
                            uint32_t mesh_index,
                            uint32_t prim_i,
                            uint32_t* out_primitive_index);

// Returns the POSITION and indices accessors for mesh primitive prim_i.
//
// On success:
//   - returns 1
//   - writes accessor indices to outputs
//   - *out_indices_accessor is set to -1 if indices are absent
//
// On failure (doc NULL, outputs NULL, out of range):
//   - returns 0
//   - output parameters are not modified
int gltf_mesh_primitive_get_accessors(const gltf_doc* doc,
                                      uint32_t mesh_index,
                                      uint32_t prim_i,
                                      uint32_t* out_position_accessor,
                                      int32_t* out_indices_accessor);

// Returns a span over the primitive POSITION accessor (VEC3).
//
// On success:
//   - returns GLTF_OK
//   - fills out_span with a valid view of positions (count = vertex count)
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - out_span is not modified
gltf_result gltf_mesh_primitive_position_span(const gltf_doc* doc,
                                              uint32_t mesh_index,
                                              uint32_t prim_i,
                                              gltf_span* out_span,
                                              gltf_error* out_err);

// Reads primitive POSITION[vertex_i] and decodes into out_xyz[3] (float).
//
// On success:
//   - returns GLTF_OK
//   - writes 3 floats to out_xyz
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid or vertex_i is out of range
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - out_xyz is not modified
gltf_result gltf_mesh_primitive_read_position_f32(const gltf_doc* doc,
                                                  uint32_t mesh_index,
                                                  uint32_t prim_i,
                                                  uint32_t vertex_i,
                                                  float out_xyz[3],
                                                  gltf_error* out_err);

// Returns non-zero if the primitive has indices.
// Returns 0 if doc is NULL, out of range, or indices are absent.
int gltf_mesh_primitive_has_indices(const gltf_doc* doc,
                                    uint32_t mesh_index,
                                    uint32_t prim_i);

// Returns the number of indices for the primitive.
//
// For indexed primitives:
//   - out_count == accessor.count of the indices accessor
//
// For non-indexed primitives:
//   - returns GLTF_OK
//   - out_count == vertex_count (same as POSITION accessor count)
//
// On success:
//   - returns GLTF_OK
//   - writes count to *out_count
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - *out_count is not modified
gltf_result gltf_mesh_primitive_index_count(const gltf_doc* doc,
                                            uint32_t mesh_index,
                                            uint32_t prim_i,
                                            uint32_t* out_count,
                                            gltf_error* out_err);

// Reads one index element and returns it as uint32.
//
// Indexed primitive:
//   - decodes the underlying index component type (U8/U16/U32) into *out_index
//
// Non-indexed primitive:
//   - returns GLTF_OK
//   - *out_index == index_i (identity mapping)
//
// On success:
//   - returns GLTF_OK
//   - writes index to *out_index
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid or index_i is out of range
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - *out_index is not modified
gltf_result gltf_mesh_primitive_read_index_u32(const gltf_doc* doc,
                                               uint32_t mesh_index,
                                               uint32_t prim_i,
                                               uint32_t index_i,
                                               uint32_t* out_index,
                                               gltf_error* out_err);

// Returns a span over the primitive indices accessor.
//
// Indexed primitive:
//   - out_span describes the raw index data (SCALAR, componentType U8/U16/U32)
//
// Non-indexed primitive:
//   - returns GLTF_OK
//   - out_span->ptr == NULL
//   - out_span->count == 0
//   - out_span->stride == 0
//   - out_span->elem_size == 0
//
// On success:
//   - returns GLTF_OK
//   - fills out_span
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - out_span is not modified
gltf_result gltf_mesh_primitive_indices_span(const gltf_doc* doc,
                                             uint32_t mesh_index,
                                             uint32_t prim_i,
                                             gltf_span* out_span,
                                             gltf_error* out_err);

// Aggregated view of a renderable primitive.
//
// This is a convenience structure that groups spans and derived metadata
// needed for drawing.
//
// If the primitive is indexed:
//   - indices.ptr != NULL
//   - index_component_type is GLTF_COMP_U8 / GLTF_COMP_U16 / GLTF_COMP_U32
//   - index_count equals indices.count
//
// If the primitive is non-indexed:
//   - indices is {NULL, 0, 0, 0}
//   - index_component_type == 0
//   - index_count equals positions.count
typedef struct gltf_draw_primitive_view {
  gltf_span positions;            // VEC3 position data
  gltf_span indices;              // SCALAR indices (may be empty for non-indexed)
  uint32_t  index_count;          // indices.count or positions.count if non-indexed
  uint32_t  index_component_type; // GLTF_COMP_U8/U16/U32, or 0 if non-indexed
} gltf_draw_primitive_view;

// Builds a draw-ready primitive view (positions + indices metadata).
//
// On success:
//   - returns GLTF_OK
//   - fills out_view
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if the primitive/accessor layout is invalid
//   - out_view is not modified
gltf_result gltf_mesh_primitive_view(const gltf_doc* doc,
                                     uint32_t mesh_index,
                                     uint32_t prim_i,
                                     gltf_draw_primitive_view* out_view,
                                     gltf_error* out_err);

// Returns primitive mode (topology).
//
// Notes:
//   - If 'mode' was omitted in JSON, the loader defaults it to GLTF_PRIM_TRIANGLES.
//   - Returns GLTF_PRIM_TRIANGLES if doc is NULL or primitive_index is out of range.
gltf_prim_mode gltf_doc_primitive_mode(const gltf_doc* doc,
                                       uint32_t primitive_index);

// Returns non-zero if the primitive has indices and writes the indices accessor index.
//
// On success (indices present):
//   - returns 1
//   - writes the accessor index to *out_indices_accessor
//
// If indices are absent:
//   - returns 0
//   - writes -1 to *out_indices_accessor
//
// On failure (doc NULL, out_indices_accessor NULL, out of range):
//   - returns 0
//   - *out_indices_accessor is not modified
int gltf_doc_primitive_indices_accessor(const gltf_doc* doc,
                                        uint32_t primitive_index,
                                        int32_t* out_indices_accessor);

// Returns the number of attributes stored on the primitive.
// Returns 0 if doc is NULL or primitive_index is out of range.
uint32_t gltf_doc_primitive_attribute_count(const gltf_doc* doc,
                                            uint32_t primitive_index);

// Returns the attribute at attr_i for a given primitive.
//
// On success:
//   - returns 1
//   - writes semantic, set index, and accessor index to output parameters
//
// On failure (doc NULL, outputs NULL, out of range):
//   - returns 0
//   - output parameters are not modified
int gltf_doc_primitive_attribute(const gltf_doc* doc,
                                 uint32_t primitive_index,
                                 uint32_t attr_i,
                                 gltf_attr_semantic* out_semantic,
                                 uint32_t* out_set_index,
                                 uint32_t* out_accessor_index);

// Finds an attribute by semantic + set index.
//
// Examples:
//   - POSITION:  semantic=GLTF_ATTR_POSITION, set_index=0
//   - TEXCOORD_0: semantic=GLTF_ATTR_TEXCOORD, set_index=0
//   - TEXCOORD_1: semantic=GLTF_ATTR_TEXCOORD, set_index=1
//
// On success:
//   - returns 1
//   - writes accessor index to *out_accessor_index
//
// If not found:
//   - returns 0
//   - *out_accessor_index is not modified
int gltf_doc_primitive_find_attribute(const gltf_doc* doc,
                                      uint32_t primitive_index,
                                      gltf_attr_semantic semantic,
                                      uint32_t set_index,
                                      uint32_t* out_accessor_index);


// ----------------------------------------------------------------------------
// Accessors
// ----------------------------------------------------------------------------

// Returns the number of accessors in the document.
// Returns 0 if doc is NULL.
uint32_t gltf_doc_accessor_count(const gltf_doc* doc);

// Returns basic metadata for the accessor at accessor_index.
//
// On success:
//   - returns 1
//   - writes accessor properties to the provided output pointers
//
// On failure (doc NULL, accessor_index out of range):
//   - returns 0
//   - output parameters are not modified
//
// Notes:
//   - out_component_type is one of gltf_component_type values
//   - out_type is one of gltf_accessor_type values
//   - out_normalized is non-zero if the accessor has normalized=true
int gltf_doc_accessor_info(const gltf_doc* doc,
                           uint32_t accessor_index,
                           uint32_t* out_count,
                           uint32_t* out_component_type,
                           uint32_t* out_type,
                           int* out_normalized);

// Returns a span-like view over the accessor's underlying buffer data.
//
// The returned span provides direct read-only access to the raw bytes
// described by the accessor, taking into account:
//   - buffer
//   - bufferView.byteOffset
//   - accessor.byteOffset
//   - bufferView.byteStride (if present)
//
// On success:
//   - returns GLTF_OK
//   - fills out_span with a valid view into document-owned memory
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if the accessor or buffer layout is invalid
//   - out_span is not modified
//
// The returned span is valid until gltf_free() is called on the document.
gltf_result gltf_accessor_span(const gltf_doc* doc,
                               uint32_t accessor_index,
                               gltf_span* out_span,
                               gltf_error* out_err);

// Reads element i of the accessor and decodes it into floating-point values.
//
// The decoded values are written to the 'out' array in component order.
// The number of components written depends on the accessor type:
//
//   SCALAR -> 1
//   VEC2   -> 2
//   VEC3   -> 3
//   VEC4   -> 4
//   MAT4   -> 16
//
// Component values are converted to float according to componentType.
// If the accessor has normalized=true, integer values are normalized
// to the range [0, 1] or [-1, 1] as defined by the glTF specification.
//
// On success:
//   - returns GLTF_OK
//   - writes decoded float values to out[]
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid or i is out of range
//   - returns GLTF_ERR_PARSE if the accessor layout is invalid
//   - out is not modified
//
// Requirements:
//   - out_cap must be large enough to hold all components
//     (16 floats is sufficient for all supported types).
gltf_result gltf_accessor_read_f32(const gltf_doc* doc,
                                   uint32_t accessor_index,
                                   uint32_t i,
                                   float* out,
                                   uint32_t out_cap,
                                   gltf_error* out_err);


// ----------------------------------------------------------------------------
// Utilities
// ----------------------------------------------------------------------------

// Computes an axis-aligned bounding box (AABB) for a VEC3 POSITION accessor.
//
// This utility reads POSITION data directly via gltf_accessor_span().
//
// Requirements:
//   - accessor must be VEC3
//   - componentType must be F32
//   - accessor must be non-normalized
//
// On success:
//   - returns GLTF_OK
//   - writes min xyz to out_min3[3]
//   - writes max xyz to out_max3[3]
//
// On failure:
//   - returns GLTF_ERR_INVALID if arguments are invalid
//   - returns GLTF_ERR_PARSE if accessor type/layout is not supported
//   - outputs are not modified
gltf_result gltf_compute_aabb_pos3_f32_span(const gltf_doc* doc,
                                           uint32_t accessor_index,
                                           float out_min3[3],
                                           float out_max3[3],
                                           gltf_error* out_err);


// ----------------------------------------------------------------------------
// Triangle iteration (primitive topology)
// ----------------------------------------------------------------------------

// One triangle expressed as 3 vertex indices.
// Indices refer to vertex elements of the primitive's attribute accessors
// (POSITION/NORMAL/TEXCOORD_0/...), i.e. the same index space as POSITION.count.
typedef struct gltf_tri {
  uint32_t i0;
  uint32_t i1;
  uint32_t i2;
} gltf_tri;

typedef enum gltf_iter_result {
  GLTF_ITER_CONTINUE = 0,
  GLTF_ITER_STOP     = 1
} gltf_iter_result;

// Called for each generated triangle.
//
// Parameters:
//   - tri: vertex indices of the triangle
//   - tri_index: 0..(triangle_count-1) within this primitive
//   - user: user context pointer passed to the iterator
//
// Return:
//   - GLTF_ITER_CONTINUE to keep iterating
//   - GLTF_ITER_STOP to stop early (iterator returns GLTF_OK)
typedef gltf_iter_result (*gltf_tri_cb)(const gltf_tri* tri,
                                       uint32_t tri_index,
                                       void* user);

// Iterates triangles produced by a primitive.
//
// Supported primitive modes:
//   - GLTF_PRIM_TRIANGLES
//   - GLTF_PRIM_TRIANGLE_STRIP
//   - GLTF_PRIM_TRIANGLE_FAN
//
// Indices:
//   - Indexed primitive: indices are decoded to uint32 (U8/U16/U32).
//   - Non-indexed primitive: identity indices [0..vertex_count).
//
// Notes:
//   - Degenerate triangles are not filtered (e.g. strip with repeated indices).
//   - Triangle winding for TRIANGLE_STRIP alternates to preserve orientation.
//
// On success:
//   - returns GLTF_OK
//   - invokes cb for each triangle until completion or early stop
//
// On failure:
//   - returns GLTF_ERR_INVALID for invalid arguments, out-of-range primitive, unsupported mode
//   - returns GLTF_ERR_PARSE for invalid index accessor layout/componentType
gltf_result gltf_doc_primitive_iterate_triangles(const gltf_doc* doc,
                                                uint32_t primitive_index,
                                                gltf_tri_cb cb,
                                                void* user,
                                                gltf_error* out_err);


// ----------------------------------------------------------------------------
// Scene graph evaluation (node transforms)
// ----------------------------------------------------------------------------

// Opaque cache for computed world matrices.
// Created per-document and reused across scenes/runs.
typedef struct gltf_world_cache gltf_world_cache;

// Creates a cache sized for the document's node count.
//
// The cache is owned by the caller and must be freed with
// gltf_world_cache_free().
//
// On success:
//   - returns GLTF_OK
//   - *out_cache is set to a valid cache
//
// On failure:
//   - returns a non-OK code
//   - *out_cache is set to NULL
//   - out_err (if non-NULL) is filled with error context
gltf_result gltf_world_cache_create(const gltf_doc* doc,
                                    gltf_world_cache** out_cache,
                                    gltf_error* out_err);

// Frees a cache created with gltf_world_cache_create().
// Safe to call with NULL.
void gltf_world_cache_free(gltf_world_cache* cache);

// Computes world matrices for all nodes reachable from scene roots.
//
// Rules:
//   - world(root) = local(root)
//   - world(child) = world(parent) * local(child)
//
// Local matrix composition for a node:
//   - if node.matrix is present, it is used and TRS is ignored
//   - otherwise local = T * R * S
//
// Matrices are column-major (glTF convention).
//
// On success:
//   - returns GLTF_OK
//
// On failure:
//   - returns a non-OK code
//   - out_err (if non-NULL) is filled with error context
gltf_result gltf_compute_world_matrices(const gltf_doc* doc,
                                       uint32_t scene_index,
                                       gltf_world_cache* cache,
                                       gltf_error* out_err);

// Returns the computed world matrix for a node (column-major).
//
// Requirements:
//   - cache must be created for this document
//   - gltf_compute_world_matrices() must have been called for a scene that
//     reaches this node
//
// On success:
//   - returns 1
//   - out_m16[16] is filled (column-major)
//
// On failure:
//   - returns 0
//   - out_m16 is not modified
int gltf_world_matrix(const gltf_doc* doc,
                      const gltf_world_cache* cache,
                      uint32_t node_index,
                      float out_m16[16]);

// Computes the node local matrix (column-major) from glTF node TRS/matrix.
//
// If node.matrix is present, TRS is ignored.
// Otherwise local = T * R * S.
//
// On success:
//   - returns 1
//   - out_m16[16] is filled
//
// On failure (doc NULL, out_m16 NULL, node_index out of range):
//   - returns 0
//   - out_m16 is not modified
int gltf_node_local_matrix(const gltf_doc* doc,
                           uint32_t node_index,
                           float out_m16[16]);


// ----------------------------------------------------------------------------
// Materials / Textures (glTF 2.0 core)
// ----------------------------------------------------------------------------
//
// This API exposes material parameters and texture references as stored in glTF.
// It does NOT decode image formats (PNG/JPEG). Images are reported as:
//   - URI (relative/absolute path),
//   - data URI,
//   - bufferView reference (typical for .glb).
//
// Texture wiring is always:
//   Material TextureInfo.index -> textures[] -> images[] (+ optional samplers[]).
// ----------------------------------------------------------------------------


// glTF texture slot reference (TextureInfo).
//
// Defaults:
//   - index   = -1 (texture slot absent)
//   - tex_coord = 0
typedef struct gltf_texture_info {
  int32_t index;      // textures[] index, -1 if absent
  int32_t tex_coord;  // TEXCOORD_<tex_coord>, default 0
} gltf_texture_info;


// glTF normal texture slot (NormalTextureInfo).
//
// Defaults:
//   - base.index = -1
//   - base.tex_coord = 0
//   - scale = 1.0
typedef struct gltf_normal_texture_info {
  gltf_texture_info base;
  float scale;        // normal scale, default 1.0
} gltf_normal_texture_info;


// glTF occlusion texture slot (OcclusionTextureInfo).
//
// Defaults:
//   - base.index = -1
//   - base.tex_coord = 0
//   - strength = 1.0
typedef struct gltf_occlusion_texture_info {
  gltf_texture_info base;
  float strength;     // occlusion strength, default 1.0
} gltf_occlusion_texture_info;


// glTF PBR metallic-roughness model (pbrMetallicRoughness).
//
// Defaults (when the pbrMetallicRoughness object is absent):
//   - base_color_factor = (1,1,1,1)
//   - metallic_factor  = 1
//   - roughness_factor = 1
//   - base_color_texture.index = -1
//   - metallic_roughness_texture.index = -1
//
// Channel convention for metallic_roughness_texture (JSON: metallicRoughnessTexture):
//   - roughness is stored in the G channel
//   - metallic  is stored in the B channel
typedef struct gltf_pbr_metallic_roughness {
  float base_color_factor[4];             // RGBA, default (1,1,1,1)
  gltf_texture_info base_color_texture;   // optional

  float metallic_factor;                 // default 1
  float roughness_factor;                // default 1
  gltf_texture_info metallic_roughness_texture; // optional
} gltf_pbr_metallic_roughness;


// Material alpha mode (alpha_mode, JSON: alphaMode).
typedef enum gltf_alpha_mode {
  GLTF_ALPHA_OPAQUE = 0,
  GLTF_ALPHA_MASK   = 1,
  GLTF_ALPHA_BLEND  = 2
} gltf_alpha_mode;


// glTF material.
//
// Defaults:
//   - name: NULL
//   - pbr.* defaults as described in gltf_pbr_metallic_roughness
//   - normal_texture.base.index = -1, tex_coord = 0, scale = 1
//   - occlusion_texture.base.index = -1, tex_coord = 0, strength = 1
//   - emissive_texture.index = -1, tex_coord = 0
//   - emissive_factor = (0,0,0)
//   - alpha_mode = OPAQUE
//   - alpha_cutoff = 0.5
//   - double_sided = 0
typedef struct gltf_material {
  const char* name; // owned by doc; valid until gltf_free()

  gltf_pbr_metallic_roughness pbr;

  gltf_normal_texture_info normal_texture;        // optional
  gltf_occlusion_texture_info occlusion_texture;  // optional

  gltf_texture_info emissive_texture;             // optional
  float emissive_factor[3];                       // default (0,0,0)

  gltf_alpha_mode alpha_mode; // default OPAQUE
  float alpha_cutoff;         // default 0.5 (used when alpha_mode == MASK)
  gltf_bool double_sided;     // boolean 0/1, default 0
} gltf_material;


// glTF sampler (texture sampling state).
//
// Defaults:
//   - wrap_s = REPEAT (10497)
//   - wrap_t = REPEAT (10497)
//   - min_filter = -1 (unspecified)
//   - mag_filter = -1 (unspecified)
typedef struct gltf_sampler {
  int32_t mag_filter; // -1 if absent, else GL enum value
  int32_t min_filter; // -1 if absent, else GL enum value
  int32_t wrap_s;     // default 10497 (REPEAT)
  int32_t wrap_t;     // default 10497 (REPEAT)
} gltf_sampler;


// Image reference kind (no decoding performed).
typedef enum gltf_image_kind {
  GLTF_IMAGE_URI = 0,         // images[i].uri is a normal URI / path
  GLTF_IMAGE_DATA_URI = 1,    // images[i].uri is a data URI (data:...)
  GLTF_IMAGE_BUFFER_VIEW = 2, // images[i].buffer_view is set (typical for .glb)
  GLTF_IMAGE_NONE = 3         // no source (rare), treat as missing
} gltf_image_kind;


// glTF image reference.
//
// For kind == GLTF_IMAGE_URI:
//   - uri is set (relative/absolute)
//   - resolved may be set (absolute filesystem path)
//
// For kind == GLTF_IMAGE_DATA_URI:
//   - uri is set (data:...)
//   - resolved is NULL
//
// For kind == GLTF_IMAGE_BUFFER_VIEW:
//   - buffer_view is set (>= 0)
//   - mimeType should be set
//   - uri/resolved may be NULL
typedef struct gltf_image {
  const char* name;       // optional, owned by doc
  gltf_image_kind kind;

  const char* uri;        // raw URI (relative, absolute, or data:)
  const char* resolved;   // resolved filesystem path for GLTF_IMAGE_URI, else NULL
  int32_t buffer_view;    // >=0 for GLTF_IMAGE_BUFFER_VIEW, else -1
  const char* mime_type;  // optional (required when bufferView is used)
} gltf_image;


// glTF texture object.
//
// References:
//   - sampler -> samplers[sampler] (optional, -1 if absent)
//   - source  -> images[source]    (optional, -1 if absent)
typedef struct gltf_texture {
  int32_t sampler; // samplers[] index, -1 if absent
  int32_t source;  // images[] index, -1 if absent
} gltf_texture;


// ----------------------------------------------------------------------------
// Materials / Textures query API
// ----------------------------------------------------------------------------


// Returns the number of materials in the document.
// If doc is NULL, returns 0.
uint32_t gltf_doc_material_count(const gltf_doc* doc);


// Returns a read-only pointer to doc-owned material storage.
//
// On success:
//   - returns 1
//   - out_mat is set to point to the material
//
// On failure (doc NULL, out_mat NULL, material_index out of range):
//   - returns 0
//   - out_mat is not modified
int gltf_doc_material(const gltf_doc* doc,
                      uint32_t material_index,
                      const gltf_material** out_mat);


// Returns the number of textures in the document.
// If doc is NULL, returns 0.
uint32_t gltf_doc_texture_count(const gltf_doc* doc);


// Returns a read-only pointer to doc-owned texture storage.
//
// On success:
//   - returns 1
//   - out_tex is set to point to the texture
//
// On failure (doc NULL, out_tex NULL, texture_index out of range):
//   - returns 0
//   - out_tex is not modified
int gltf_doc_texture(const gltf_doc* doc,
                     uint32_t texture_index,
                     const gltf_texture** out_tex);


// Returns the number of images in the document.
// If doc is NULL, returns 0.
uint32_t gltf_doc_image_count(const gltf_doc* doc);


// Returns a read-only pointer to doc-owned image storage.
//
// On success:
//   - returns 1
//   - out_img is set to point to the image
//
// On failure (doc NULL, out_img NULL, image_index out of range):
//   - returns 0
//   - out_img is not modified
int gltf_doc_image(const gltf_doc* doc,
                   uint32_t image_index,
                   const gltf_image** out_img);


// Returns the number of samplers in the document.
// If doc is NULL, returns 0.
uint32_t gltf_doc_sampler_count(const gltf_doc* doc);


// Returns a read-only pointer to doc-owned sampler storage.
//
// On success:
//   - returns 1
//   - out_samp is set to point to the sampler
//
// On failure (doc NULL, out_samp NULL, sampler_index out of range):
//   - returns 0
//   - out_samp is not modified
int gltf_doc_sampler(const gltf_doc* doc,
                     uint32_t sampler_index,
                     const gltf_sampler** out_samp);


// Returns a resolved image reference string suitable for reporting/logging.
//
// For GLTF_IMAGE_URI:
//   - returns image.resolved if available, otherwise returns image.uri
//
// For GLTF_IMAGE_DATA_URI:
//   - returns image.uri (data:...)
//
// For GLTF_IMAGE_BUFFER_VIEW:
//   - returns NULL
//
// For GLTF_IMAGE_NONE:
//   - returns NULL
//
// On failure (doc NULL, image_index out of range):
//   - returns NULL
const char* gltf_image_resolved_uri(const gltf_doc* doc, uint32_t image_index);


// ----------------------------------------------------------------------------
// Images (decode / export)
// ----------------------------------------------------------------------------
// Decodes PNG/JPEG (and other formats supported by the compiled backend)
// from glTF image sources:
// - external URI files
// - embedded base64 data URI
// - GLB BIN via bufferView
//
// Note: if the library is built without image decoding support,
// functions return GLTF_ERR_UNSUPPORTED.

typedef enum gltf_image_pixel_format {
  GLTF_PIXEL_RGBA8 = 1
} gltf_image_pixel_format;

typedef struct gltf_image_pixels {
  gltf_image_pixel_format format; // always GLTF_PIXEL_RGBA8 in this iteration
  uint32_t width;
  uint32_t height;
  uint32_t stride_bytes; // width * 4
  uint8_t* pixels;       // owned by caller; free via gltf_image_pixels_free()
} gltf_image_pixels;

// Decode doc.images[image_index] into RGBA8 pixels.
// On success: returns GLTF_OK and fills out_pixels.
// On failure: returns non-OK and out_pixels is not modified.
gltf_result gltf_image_decode_rgba8(const gltf_doc* doc,
                                    uint32_t image_index,
                                    gltf_image_pixels* out_pixels,
                                    gltf_error* out_err);

// Convenience: write RGBA8 buffer as PNG to disk (used by examples).
gltf_result gltf_write_png_rgba8(const char* path,
                                 uint32_t width,
                                 uint32_t height,
                                 const uint8_t* rgba_pixels,
                                 gltf_error* out_err);

// Frees pixels allocated by gltf_image_decode_rgba8().
// Safe to call with NULL or with p->pixels == NULL.
void gltf_image_pixels_free(gltf_image_pixels* p);


#ifdef __cplusplus
}
#endif
