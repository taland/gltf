#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
  GLTF_ERR_IO,      // file I/O or allocation failure
  GLTF_ERR_PARSE,   // invalid JSON or wrong types/structure
  GLTF_ERR_INVALID  // invalid arguments passed by the caller
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
// Mesh primitives (POSITION/indices)
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

#ifdef __cplusplus
}
#endif
