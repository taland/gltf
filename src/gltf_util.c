// Minimal educational glTF 2.0 loader (C11).
//
// This module contains small utility helpers built on top of the core APIs.
//
// Responsibilities:
//   - compute derived values from accessor spans (e.g. bounds)
//
// Notes:
//   - Public API contracts are documented in include/gltf/gltf.h.
//   - Utilities may decode through the accessor/span helpers.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Utilities
// ----------------------------------------------------------------------------

// Computes an AABB for the first three components of each element.
//
// Notes:
//   - Uses gltf_decode_component_to_f32(); integer/normalized accessors are
//     decoded as floats.


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
  // Initialize bounds from element 0.
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
