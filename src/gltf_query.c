// Minimal educational glTF 2.0 loader (C11).
//
// This module implements basic document queries (names + simple indices).
//
// Responsibilities:
//   - expose counts and names for scenes/nodes/meshes
//   - expose scene/node index relationships stored in the shared index pool
//
// Notes:
//   - Public API contracts are documented in include/gltf/gltf.h.
//   - Indirection arrays (scene.nodes, node.children) are expanded into u32 lists.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Asset
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


// ----------------------------------------------------------------------------
// Scenes
// ----------------------------------------------------------------------------

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


// ----------------------------------------------------------------------------
// Nodes
// ----------------------------------------------------------------------------

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


// ----------------------------------------------------------------------------
// Meshes
// ----------------------------------------------------------------------------

uint32_t gltf_doc_mesh_count(const gltf_doc* doc) {
  return doc ? doc->mesh_count : 0u;
}

const char* gltf_doc_mesh_name(const gltf_doc* doc, uint32_t mesh_index) {
  if (!doc) return NULL;
  if (mesh_index >= doc->mesh_count) return NULL;
  return arena_get_str(&doc->arena, doc->meshes[mesh_index].name);
}
