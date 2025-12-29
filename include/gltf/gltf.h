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

#ifdef __cplusplus
}
#endif
