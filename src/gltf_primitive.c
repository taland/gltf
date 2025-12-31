// Minimal educational glTF 2.0 loader (C11).
//
// This module implements mesh primitive queries for POSITION and indices.
//
// Responsibilities:
//   - map mesh + prim_i to a primitive index
//   - expose POSITION/indices accessors and spans
//   - read individual POSITION/indices elements (decoded)
//
// Notes:
//   - Public API contracts are documented in include/gltf/gltf.h.
//   - This loader exposes only POSITION and optional indices.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Mesh primitives (POSITION/indices)
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
