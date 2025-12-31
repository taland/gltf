// Minimal educational glTF 2.0 loader (C11).
//
// This module implements accessor data access helpers.
//
// Responsibilities:
//   - validate accessor/bufferView ranges and byte layout
//   - compute a safe span (ptr/count/stride/elem_size) over document-owned data
//   - decode a single accessor element into floats
//
// Notes:
//   - This file does not define the public contracts; see include/gltf/gltf.h.
//   - All memory is owned by the document; spans are views only.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Accessors (spans + decode)
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

  // base is absolute (buffer space); compute the relative offset inside bufferView.
  const size_t rel = (size_t)a->byte_offset;

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
