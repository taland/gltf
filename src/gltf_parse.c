// Minimal educational glTF 2.0 loader (C11).
//
// This module provides small JSON parsing helpers used by the loader.
//
// Responsibilities:
//   - read/validate common JSON fields (scalars, arrays)
//   - expand index arrays into a shared u32 pool
//   - decode base64 data: URIs for embedded buffers
//
// Notes:
//   - These helpers are internal; public API contracts live in include/gltf/gltf.h.
//   - Error paths are passed in by the caller to keep messages consistent.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Internal helpers (base64)
// ----------------------------------------------------------------------------

// Base64 helpers implemented in src/base64.c.
size_t gltf_base64_max_decoded_size(size_t in_len);

int gltf_base64_decode(const char* in,
                       size_t in_len,
                       uint8_t* out,
                       size_t out_cap,
                       size_t* out_len);


// ----------------------------------------------------------------------------
// JSON parsing helpers
// ----------------------------------------------------------------------------

// Optional scalars

gltf_result gltf_json_get_u32(yyjson_val* obj,
                              const char* key,
                              uint32_t default_value,
                              uint32_t* out,
                              const char* err_path,
                              gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }
  if (!yyjson_is_uint(v)) {
    gltf_set_err(out_err, "must be an unsigned integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint64_t x = yyjson_get_uint(v);
  if (x > UINT32_MAX) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (uint32_t)x;
  return GLTF_OK;
}

gltf_result gltf_json_get_i32(yyjson_val* obj,
                              const char* key,
                              int32_t default_value,
                              int32_t* out,
                              const char* err_path,
                              gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }
  if (!yyjson_is_int(v)) {
    gltf_set_err(out_err, "must be an integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  int64_t x = yyjson_get_int(v);
  if (x > INT32_MAX || x < INT32_MIN) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (int32_t)x;
  return GLTF_OK;
}

gltf_result gltf_json_get_bool(yyjson_val* obj,
                               const char* key,
                               int default_value,
                               uint8_t* out,
                               const char* err_path,
                               gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value ? 1 : 0;
    return GLTF_OK;
  }

  if (!yyjson_is_bool(v)) {
    gltf_set_err(out_err, "must be boolean", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = yyjson_get_bool(v) ? 1u : 0u;
  return GLTF_OK;
}

gltf_result gltf_json_get_str_opt_dup_arena(yyjson_val* obj,
                                            const char* key,
                                            gltf_arena* arena,
                                            gltf_str* out,
                                            const char* err_path,
                                            gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = gltf_str_invalid();
    return GLTF_OK;
  }
  if (!yyjson_is_str(v)) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* s = yyjson_get_str(v);
  // yyjson string should not be NULL if yyjson_is_str(v)
  if (!s) {
    *out = gltf_str_invalid();
    return GLTF_OK;
  }

  gltf_str gs = arena_strdup(arena, s);
  if (!gltf_str_is_valid(gs)) {
    gltf_set_err(out_err, "out of memory", err_path, 1, 1);
    return GLTF_ERR_IO;
  }

  *out = gs;
  return GLTF_OK;
}

// Required scalars

gltf_result gltf_json_get_u32_req(yyjson_val* obj,
                                  const char* key,
                                  uint32_t* out,
                                  const char* err_path,
                                  gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    gltf_set_err(out_err, "must be present", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!yyjson_is_uint(v)) {
    gltf_set_err(out_err, "must be an unsigned integer", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint64_t x = yyjson_get_uint(v);
  if (x > UINT32_MAX) {
    gltf_set_err(out_err, "integer out of range", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (uint32_t)x;
  return GLTF_OK;
}

gltf_result gltf_json_get_accessor_type_required(yyjson_val* obj,
                                                 const char* key,
                                                 uint8_t* out,
                                                 const char* err_path,
                                                 gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    gltf_set_err(out_err, "must be present", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }
  if (!yyjson_is_str(v)) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* s = yyjson_get_str(v);
  if (!s) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (strcmp(s, "SCALAR") == 0) {
    *out = GLTF_ACCESSOR_SCALAR;
  } else if (strcmp(s, "VEC2") == 0) {
    *out = GLTF_ACCESSOR_VEC2;
  } else if (strcmp(s, "VEC3") == 0) {
    *out = GLTF_ACCESSOR_VEC3;
  } else if (strcmp(s, "VEC4") == 0) {
    *out = GLTF_ACCESSOR_VEC4;
  } else if (strcmp(s, "MAT4") == 0) {
    *out = GLTF_ACCESSOR_MAT4;
  } else {
    gltf_set_err(out_err, "invalid accessor type", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  return GLTF_OK;
}

// Arrays (index lists)

gltf_result gltf_json_get_u32_index_array_range_opt(gltf_doc* doc,
                                                    yyjson_val* obj,
                                                    const char* key,
                                                    gltf_range_u32* out_range,
                                                    const char* err_path_arr,
                                                    const char* err_path_elem,
                                                    gltf_error* out_err) {
  if (!doc || !out_range) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    out_range->first = 0;
    out_range->count = 0;
    return GLTF_OK;
  }

  if (!yyjson_is_arr(v)) {
    gltf_set_err(out_err, "must be array", err_path_arr, 1, 1);
    return GLTF_ERR_PARSE;
  }

  uint32_t first = doc->indices_count;

  size_t idx, nmax;
  yyjson_val* it = NULL;
  yyjson_arr_foreach(v, idx, nmax, it) {
    if (!yyjson_is_uint(it)) {
      gltf_set_err(out_err, "must be unsigned integer", err_path_elem, 1, 1);
      return GLTF_ERR_PARSE;
    }

    uint64_t x = yyjson_get_uint(it);
    if (x > UINT32_MAX) {
      gltf_set_err(out_err, "integer out of range", err_path_elem, 1, 1);
      return GLTF_ERR_PARSE;
    }

    if (!indices_push_u32(doc, (uint32_t)x)) {
      gltf_set_err(out_err, "out of memory", err_path_elem, 1, 1);
      return GLTF_ERR_IO;
    }
  }

  out_range->first = first;
  out_range->count = (uint32_t)(doc->indices_count - first);
  return GLTF_OK;
}

static int gltf_parse_uint_suffix(const char* p, uint32_t* out_n) {
  if (!p || !out_n) return 0;
  if (*p == '\0') return 0;

  char* end = NULL;
  unsigned long n = strtoul(p, &end, 10);

  // must consume at least one digit and then end string
  if (end == p || *end != '\0') return 0;
  if (n > UINT32_MAX) return 0;

  *out_n = (uint32_t)n;
  return 1;
}

gltf_attr_semantic gltf_parse_semantic(const char* key, uint32_t* out_set_index) {
  if (!out_set_index) return GLTF_ATTR_UNKNOWN;
  uint32_t n = 0;
  *out_set_index = n;

  if (!key) return GLTF_ATTR_UNKNOWN;

  // fixed, non-indexed semantics
  if (strcmp(key, "POSITION") == 0) return GLTF_ATTR_POSITION;
  if (strcmp(key, "NORMAL")   == 0) return GLTF_ATTR_NORMAL;
  if (strcmp(key, "TANGENT")  == 0) return GLTF_ATTR_TANGENT;

  // TEXCOORD_n
  if (strncmp(key, "TEXCOORD_", 9) == 0) {
    if (!gltf_parse_uint_suffix(key + 9, &n)) return GLTF_ATTR_UNKNOWN;
    *out_set_index = n;
    return GLTF_ATTR_TEXCOORD;
  }

  // COLOR_n
  if (strncmp(key, "COLOR_", 6) == 0) {
    if (!gltf_parse_uint_suffix(key + 6, &n)) return GLTF_ATTR_UNKNOWN;
    *out_set_index = n;
    return GLTF_ATTR_COLOR;
  }

  // JOINTS_n
  if (strncmp(key, "JOINTS_", 7) == 0) {
    if (!gltf_parse_uint_suffix(key + 7, &n)) return GLTF_ATTR_UNKNOWN;
    *out_set_index = n;
    return GLTF_ATTR_JOINTS;
  }

  // WEIGHTS_n
  if (strncmp(key, "WEIGHTS_", 8) == 0) {
    if (!gltf_parse_uint_suffix(key + 8, &n)) return GLTF_ATTR_UNKNOWN;
    *out_set_index = n;
    return GLTF_ATTR_WEIGHTS;
  }

  return GLTF_ATTR_UNKNOWN;
}


// ----------------------------------------------------------------------------
// Data URIs (base64)
// ----------------------------------------------------------------------------

gltf_result gltf_decode_data_uri(const char* uri,
                                 uint32_t expected_len,
                                 uint8_t** out_bytes,
                                 uint32_t* out_len,
                                 gltf_error* out_err) {
  if (!uri || !out_bytes || !out_len) {
    gltf_set_err(out_err, "invalid arguments", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_INVALID;
  }

  *out_bytes = NULL;
  *out_len = 0;

  if (strncmp(uri, "data:", 5) != 0) {
    gltf_set_err(out_err, "buffer uri must start with 'data:'", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* tag = ";base64,";
  const char* p = strstr(uri, tag);
  if (!p) {
    gltf_set_err(out_err, "only base64 data URIs are supported", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* payload = p + strlen(tag);
  const size_t payload_len = strlen(payload);

  // Decide allocation size.
  size_t cap = 0;
  if (expected_len != 0) {
    cap = (size_t)expected_len;
  } else {
    cap = gltf_base64_max_decoded_size(payload_len);
    if (cap == SIZE_MAX) {
      gltf_set_err(out_err, "data uri payload too large", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_PARSE;
    }
  }

  uint8_t* bytes = NULL;
  if (cap > 0) {
    bytes = (uint8_t*)malloc(cap);
    if (!bytes) {
      gltf_set_err(out_err, "out of memory allocating decoded buffer", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_IO;
    }
  }

  size_t decoded_len = 0;
  int ok = gltf_base64_decode(payload, payload_len, bytes, cap, &decoded_len);
  if (!ok) {
    free(bytes);
    gltf_set_err(out_err, "invalid base64 payload", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (decoded_len > UINT32_MAX) {
    free(bytes);
    gltf_set_err(out_err, "decoded buffer too large", "root.buffers[].uri", 1, 1);
    return GLTF_ERR_PARSE;
  }

  if (expected_len != 0 && decoded_len != (size_t)expected_len) {
    free(bytes);
    gltf_set_err(out_err,
                 "decoded buffer length does not match byteLength",
                 "root.buffers[].byteLength",
                 1,
                 1);
    return GLTF_ERR_PARSE;
  }

  *out_bytes = bytes;
  *out_len = (uint32_t)decoded_len;
  return GLTF_OK;
}
