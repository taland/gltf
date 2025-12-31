// Minimal educational glTF 2.0 loader (C11).
//
// This module implements small binary decoding helpers used by parsing and
// accessor/span code.
//
// Responsibilities:
//   - read little-endian scalars from unaligned byte streams
//   - map accessor type/componentType to sizes and component counts
//   - decode a single component to float (optionally normalized)
//
// Notes:
//   - This file does not define the public contracts; see include/gltf/gltf.h.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Endian utilities
// ----------------------------------------------------------------------------

uint16_t rd_u16_le(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8u);
}

uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

// ----------------------------------------------------------------------------
// Component metadata
// ----------------------------------------------------------------------------

int gltf_accessor_component_count(uint32_t accessor_type, uint32_t* out_count) {
  if (!out_count) return 0;
  switch (accessor_type) {
  case GLTF_ACCESSOR_SCALAR: *out_count = 1; break;
  case GLTF_ACCESSOR_VEC2:   *out_count = 2; break;
  case GLTF_ACCESSOR_VEC3:   *out_count = 3; break;
  case GLTF_ACCESSOR_VEC4:   *out_count = 4; break;
  case GLTF_ACCESSOR_MAT2:   *out_count = 4; break;
  case GLTF_ACCESSOR_MAT3:   *out_count = 9; break;
  case GLTF_ACCESSOR_MAT4:   *out_count = 16; break;
  default: return 0;
  }
  return 1;
}

int gltf_component_size_bytes(uint32_t component_type, uint32_t* out_size) {
  if (!out_size) return 0;
  switch (component_type) {
  case GLTF_COMP_I8:
  case GLTF_COMP_U8:  *out_size = 1; break;
  case GLTF_COMP_I16:
  case GLTF_COMP_U16: *out_size = 2; break;
  case GLTF_COMP_U32:
  case GLTF_COMP_F32: *out_size = 4; break;
  default: return 0;
  }
  return 1;
}

// ----------------------------------------------------------------------------
// Component decode
// ----------------------------------------------------------------------------

// Decodes exactly one component value to float.
//
// Notes:
//   - Input is interpreted as little-endian and may be unaligned.
//   - For signed normalized integers, MIN maps to -1.0f (glTF normalization).

gltf_result gltf_decode_component_to_f32(const uint8_t* p,
                                         uint32_t component_type,
                                         int normalized,
                                         float* out) {
  if (!p || !out) return GLTF_ERR_INVALID;

  switch (component_type) {
  case GLTF_COMP_F32: {
    uint32_t u = rd_u32_le(p);
    float f;
    memcpy(&f, &u, sizeof f);
    *out = f;
    return GLTF_OK;
  }

  case GLTF_COMP_U8: {
    uint32_t v = (uint32_t)p[0];
    *out = normalized ? (float)v / 255.0f : (float)v;
    return GLTF_OK;
  }

  case GLTF_COMP_I8: {
    int8_t v = (int8_t)p[0];
    if (normalized) {
      *out = (v == (int8_t)-128) ? -1.0f : (float)v / 127.0f;
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  case GLTF_COMP_U16: {
    uint32_t v = (uint32_t)rd_u16_le(p);
    *out = normalized ? (float)v / 65535.0f : (float)v;
    return GLTF_OK;
  }

  case GLTF_COMP_I16: {
    int16_t v = (int16_t)rd_u16_le(p);
    if (normalized) {
      *out = (v == (int16_t)-32768) ? -1.0f : (float)v / 32767.0f;
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  case GLTF_COMP_U32: {
    uint32_t v = rd_u32_le(p);
    if (normalized) {
      *out = (float)((double)v / 4294967295.0);
    } else {
      *out = (float)v;
    }
    return GLTF_OK;
  }

  default:
    return GLTF_ERR_PARSE;
  }
}
