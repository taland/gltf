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


static gltf_result gltf_read_index_u32_from_accessor(const gltf_doc* doc,
                                                     uint32_t indices_accessor,
                                                     uint32_t index_i,
                                                     uint32_t* out,
                                                     gltf_error* out_err) {
  if (!doc || !out) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (indices_accessor >= doc->accessor_count) {
    gltf_set_err(out_err, "accessor out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  const gltf_accessor* a = &doc->accessors[indices_accessor];

  // indices must be SCALAR and non-normalized
  if (a->type != GLTF_ACCESSOR_SCALAR) {
    gltf_set_err(out_err, "indices accessor not SCALAR", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (a->normalized) {
    gltf_set_err(out_err, "indices accessor must not be normalized", "root.accessors[].normalized", 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!(a->component_type == GLTF_COMP_U8 ||
        a->component_type == GLTF_COMP_U16 ||
        a->component_type == GLTF_COMP_U32)) {
    gltf_set_err(out_err, "indices componentType not U8/U16/U32", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (index_i >= a->count) {
    gltf_set_err(out_err, "index out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  gltf_span span;
  gltf_result r = gltf_accessor_span(doc, indices_accessor, &span, out_err);
  if (r != GLTF_OK) {
    gltf_set_err(out_err, "failed to get indices accessor span", "root.accessors[]", 1, 1);
    return r;
  }
  if (!span.ptr) {
    gltf_set_err(out_err, "indices span is null", "root.accessors[]", 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t comp_size = 0;
  if (!gltf_component_size_bytes(a->component_type, &comp_size)) {
    gltf_set_err(out_err, "invalid componentType", "root.accessors[].componentType", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const uint32_t stride = (span.stride != 0) ? span.stride : comp_size;
  const uint8_t* p = span.ptr + (size_t)index_i * (size_t)stride;

  switch (a->component_type) {
    case GLTF_COMP_U8:  *out = (uint32_t)p[0]; return GLTF_OK;
    case GLTF_COMP_U16: *out = (uint32_t)rd_u16_le(p); return GLTF_OK;
    case GLTF_COMP_U32: *out = rd_u32_le(p); return GLTF_OK;
    default: break;
  }

  gltf_set_err(out_err, "invalid index componentType", "root.accessors[].componentType", 1, 1);
  return GLTF_ERR_PARSE;
}

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
  if (!doc || !out_position_accessor || !out_indices_accessor) return 0;
  if (mesh_index >= doc->mesh_count) return 0;

  const gltf_mesh* mesh = &doc->meshes[mesh_index];

  if (prim_i >= mesh->primitive_count) return 0;

  const uint32_t primitive_index = mesh->primitive_first + prim_i;
  uint32_t pos_accessor = 0;

  if (!gltf_doc_primitive_find_attribute(
      doc,
      primitive_index,
      GLTF_ATTR_POSITION,
      0,
      &pos_accessor)) {
    return 0;
  }

  *out_position_accessor = pos_accessor;

  int32_t idx_accessor = -1;
  (void)gltf_doc_primitive_indices_accessor(doc, primitive_index, &idx_accessor);
  *out_indices_accessor = idx_accessor;

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

  const uint32_t primitive_index = mesh->primitive_first + prim_i;
  uint32_t pos_accessor = 0;

  if (!gltf_doc_primitive_find_attribute(
      doc,
      primitive_index,
      GLTF_ATTR_POSITION,
      0,
      &pos_accessor)) {
    gltf_set_err(
      out_err,
      "primitive has no POSITION attribute",
      "root.meshes[].primitives[].attributes.POSITION",
      1,
      1
    );
    return GLTF_ERR_PARSE;
  }

  return gltf_accessor_span(doc, pos_accessor, out_span, out_err);
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

  const uint32_t primitive_index = mesh->primitive_first + prim_i;
  uint32_t pos_accessor = 0;
  gltf_span span;

  if (!gltf_doc_primitive_find_attribute(
      doc,
      primitive_index,
      GLTF_ATTR_POSITION,
      0,
      &pos_accessor)) {
    gltf_set_err(
      out_err,
      "primitive has no POSITION attribute",
      "root.meshes[].primitives[].attributes.POSITION",
      1,
      1
    );
    return GLTF_ERR_PARSE;
  }

  gltf_result r = gltf_accessor_span(doc, pos_accessor, &span, out_err);
  if (r != GLTF_OK) {
    gltf_set_err(out_err, "failed to get position accessor span", "root.accessors[]", 1, 1);
    return r;
  }

  const gltf_accessor* a = &doc->accessors[pos_accessor];
  if (vertex_i >= a->count) {
    gltf_set_err(out_err, "vertex index out of range", "root.accessors[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  if (a->type != GLTF_ACCESSOR_VEC3) {
    gltf_set_err(out_err, "position accessor is not VEC3", "root.accessors[].type", 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t comp_count = 0;
  if (!gltf_accessor_component_count(a->type, &comp_count)) {
    gltf_set_err(out_err, "invalid accessor type", "root.accessors[].type", 1, 1);
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
    gltf_result r = gltf_decode_component_to_f32(
      vertex_ptr + (size_t)c * (size_t)comp_size,
      a->component_type,
      a->normalized,
      &out_xyz[c]
    );

    if (r != GLTF_OK) {
      gltf_set_err(out_err, "failed to decode position component", "root.accessors[]", 1, 1);
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

  return gltf_read_index_u32_from_accessor(doc, (uint32_t)prim->indices_accessor, index_i, out_index, out_err);
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

gltf_prim_mode gltf_doc_primitive_mode(const gltf_doc* doc,
                                       uint32_t primitive_index) {
  if (!doc) return GLTF_PRIM_TRIANGLES;
  if (primitive_index >= doc->primitive_count) return GLTF_PRIM_TRIANGLES;
  return doc->primitives[primitive_index].mode;
}

int gltf_doc_primitive_indices_accessor(const gltf_doc* doc,
                                        uint32_t primitive_index,
                                        int32_t* out_indices_accessor) {
  if (!doc || !out_indices_accessor) return 0;
  if (primitive_index >= doc->primitive_count) return 0;

  const gltf_primitive* prim = &doc->primitives[primitive_index];
  *out_indices_accessor = prim->indices_accessor;
  
  return (prim->indices_accessor >= 0);
}

uint32_t gltf_doc_primitive_attribute_count(const gltf_doc* doc,
                                            uint32_t primitive_index) {
  if (!doc) return 0;
  if (primitive_index >= doc->primitive_count) return 0;
  const gltf_primitive* prim = &doc->primitives[primitive_index];
  return prim->attributes_count;
}

int gltf_doc_primitive_attribute(const gltf_doc* doc,
                                 uint32_t primitive_index,
                                 uint32_t attr_i,
                                 gltf_attr_semantic* out_semantic,
                                 uint32_t* out_set_index,
                                 uint32_t* out_accessor_index) {
  if (!doc || !out_semantic || !out_set_index || !out_accessor_index) return 0;
  if (primitive_index >= doc->primitive_count) return 0;

  const gltf_primitive* prim = &doc->primitives[primitive_index];
  uint32_t base = prim->attributes_first;
  uint32_t count = prim->attributes_count;
  
  if (base > doc->prim_attr_count) return 0;
  if (count > doc->prim_attr_count - base) return 0;
  if (attr_i >= count) return 0;
  
  const gltf_prim_attr* attr = &doc->prim_attrs[base + attr_i];
  *out_semantic = attr->semantic;
  *out_set_index = attr->set_index;
  *out_accessor_index = attr->accessor_index;
  
  return 1;
}

int gltf_doc_primitive_find_attribute(const gltf_doc* doc,
                                      uint32_t primitive_index,
                                      gltf_attr_semantic semantic,
                                      uint32_t set_index,
                                      uint32_t* out_accessor_index) {
  if (!doc || !out_accessor_index) return 0;
  if (primitive_index >= doc->primitive_count) return 0;

  const gltf_primitive* prim = &doc->primitives[primitive_index];
  
  for (uint32_t i = 0; i < prim->attributes_count; i++) {
    const gltf_prim_attr* attr = &doc->prim_attrs[prim->attributes_first + i];
    if (attr->semantic == semantic && attr->set_index == set_index) {
      *out_accessor_index = attr->accessor_index;
      return 1;
    }
  }

  return 0;
}

gltf_result gltf_doc_primitive_iterate_triangles(const gltf_doc* doc,
                                                uint32_t primitive_index,
                                                gltf_tri_cb cb,
                                                void* user,
                                                gltf_error* out_err) {
  if (!doc || !cb) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }
  if (primitive_index >= doc->primitive_count) {
    gltf_set_err(out_err, "primitive out of range", "root.primitives[]", 1, 1);
    return GLTF_ERR_INVALID;
  }

  uint32_t pos_accessor = 0;
  int ret = gltf_doc_primitive_find_attribute(doc, primitive_index, GLTF_ATTR_POSITION, 0, &pos_accessor);
  if (!ret) {
    gltf_set_err(
      out_err,
      "primitive has no POSITION attribute",
      "root.meshes[].primitives[].attributes.POSITION",
      1,
      1
    );
    return GLTF_ERR_PARSE;
  }

  uint32_t v_count = 0, v_comp = 0, v_type = 0;
  int v_norm = 0;

  ret = gltf_doc_accessor_info(doc, pos_accessor, &v_count, &v_comp, &v_type, &v_norm);
  if (!ret) {
    gltf_set_err(
      out_err,
      "failed to get POSITION accessor info",
      "root.accessors[]",
      1,
      1
    );
    return GLTF_ERR_PARSE;
  }

  if (v_count == 0) return GLTF_OK;
  if (v_type != GLTF_ACCESSOR_VEC3) return GLTF_ERR_PARSE;
  if (v_comp != GLTF_COMP_F32) return GLTF_ERR_PARSE;
  if (v_norm) return GLTF_ERR_PARSE;

  int32_t idx_accessor = -1;
  int has_idx = gltf_doc_primitive_indices_accessor(doc, primitive_index, &idx_accessor);
  
  uint32_t i_count = 0, i_comp = 0, i_type = 0;
  int i_norm = 0;

  if (has_idx) {
    ret = gltf_doc_accessor_info(doc, (uint32_t)idx_accessor, &i_count, &i_comp, &i_type, &i_norm);

    if (!ret) {
      gltf_set_err(out_err, "failed to get indices accessor info", "root.accessors[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    if (i_type != GLTF_ACCESSOR_SCALAR) {
      gltf_set_err(out_err, "indices accessor not SCALAR", "root.accessors[].type", 1, 1);
      return GLTF_ERR_PARSE;
    }

    if (i_norm) {
      gltf_set_err(out_err, "indices accessor must not be normalized", "root.accessors[].normalized", 1, 1);
      return GLTF_ERR_PARSE;
    }

    if (!(i_comp == GLTF_COMP_U8 || i_comp == GLTF_COMP_U16 || i_comp == GLTF_COMP_U32)) {
      gltf_set_err(out_err, "indices componentType not U8/U16/U32", "root.accessors[].componentType", 1, 1);
      return GLTF_ERR_PARSE;
    }
  }
  
  if (doc->primitives[primitive_index].mode != GLTF_PRIM_TRIANGLES) {
    gltf_set_err(out_err, "only TRIANGLES primitive mode is supported", "root.primitives[].mode", 1, 1);
    return GLTF_ERR_INVALID;
  }

  uint32_t n = has_idx ? i_count : v_count;
  if ((n % 3) != 0) {
    gltf_set_err(out_err, "TRIANGLES require count divisible by 3", "root.primitives[]", 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t tri_count = n / 3;
  
  for (uint32_t t = 0; t < tri_count; t++) {
    gltf_tri tri;

    if (has_idx) {
      gltf_result r;

      r = gltf_read_index_u32_from_accessor(doc, (uint32_t)idx_accessor, t * 3 + 0, &tri.i0, out_err);
      if (r != GLTF_OK) {
        return r;
      }

      r = gltf_read_index_u32_from_accessor(doc, (uint32_t)idx_accessor, t * 3 + 1, &tri.i1, out_err);
      if (r != GLTF_OK) {
        return r;
      }

      r = gltf_read_index_u32_from_accessor(doc, (uint32_t)idx_accessor, t * 3 + 2, &tri.i2, out_err);
      if (r != GLTF_OK) {
        return r;
      }

      if (tri.i0 >= v_count || tri.i1 >= v_count || tri.i2 >= v_count) {
        gltf_set_err(out_err, "index out of range", "root.accessors[]", 1, 1);
        return GLTF_ERR_PARSE;
      }
    } else {
      tri.i0 = t * 3 + 0;
      tri.i1 = t * 3 + 1;
      tri.i2 = t * 3 + 2;
    }

    gltf_iter_result cb_r = cb(&tri, t, user);
    if (cb_r == GLTF_ITER_STOP) {
      return GLTF_OK;
    }
  }


  return GLTF_OK;
}
