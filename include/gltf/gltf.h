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

#ifdef __cplusplus
}
#endif
