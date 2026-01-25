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
// Internal helpers (fs)
// ----------------------------------------------------------------------------

// File system helpers implemented in src/fs.c.
typedef enum gltf_fs_status {
  GLTF_FS_OK = 0,
  GLTF_FS_INVALID,
  GLTF_FS_IO,
  GLTF_FS_OOM,
  GLTF_FS_SIZE_MISMATCH,
  GLTF_FS_TOO_LARGE
} gltf_fs_status;

size_t gltf_fs_dir_len(const char* path);

char* gltf_fs_join_dir_leaf(const char* dir_prefix,
                            size_t dir_len,
                            const char* leaf);

gltf_fs_status gltf_fs_read_file_exact_u32(const char* path,
                                           uint32_t expected_len,
                                           uint8_t** out_data,
                                           uint32_t* out_len);

int gltf_path_is_relative(const char* path);


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

/*
 * Exception safety:
 * -----------------
 * Values are written directly into `out`.
 * On error, contents of `out` are undefined.
 *
 * Contract:
 *   - `out` is valid only if GLTF_OK is returned.
 *   - No heap allocations or temporary buffers are used.
 *
 * Rationale:
 *   - Simpler code
 *   - Better performance
 *   - Allocation-free parsing
 */
static gltf_result gltf_json_get_f32_array_opt_n(yyjson_val* obj,
                                                 const char* key,
                                                 const float* default_value,
                                                 float* out,
                                                 uint32_t n,
                                                 const char* err_path,
                                                 gltf_error* out_err) {
  if (!out || !default_value || n == 0) {
    gltf_set_err(out_err, "invalid arguments", err_path, 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    memcpy(out, default_value, (size_t)n * sizeof(float));
    return GLTF_OK;
  }
  if (!yyjson_is_arr(v)) {
    gltf_set_err(out_err, "must be an array", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const uint32_t s = (uint32_t)yyjson_arr_size(v);
  if (s != n) {
    gltf_set_err(out_err, "array has wrong length", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  size_t idx, max;
  yyjson_val* val = NULL;
  yyjson_arr_foreach(v, idx, max, val) {
    if (!yyjson_is_num(val)) {
      gltf_set_err(out_err, "must be a number", err_path, 1, 1);
      return GLTF_ERR_PARSE;
    }
    out[idx] = (float)yyjson_get_num(val);
  }

  return GLTF_OK;
}

gltf_result gltf_json_get_vec3_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[3],
                                       float out[3],
                                       const char* err_path,
                                       gltf_error* out_err) {
  return gltf_json_get_f32_array_opt_n(obj, key, default_value, out, 3u, err_path, out_err);
}

gltf_result gltf_json_get_vec4_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[4],
                                       float out[4],
                                       const char* err_path,
                                       gltf_error* out_err) {
  return gltf_json_get_f32_array_opt_n(obj, key, default_value, out, 4u, err_path, out_err);
}

gltf_result gltf_json_get_mat4_f32_opt(yyjson_val* obj,
                                       const char* key,
                                       const float default_value[16],
                                       float out[16],
                                       const char* err_path,
                                       gltf_error* out_err) {
  return gltf_json_get_f32_array_opt_n(obj, key, default_value, out, 16u, err_path, out_err);
}

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

gltf_result gltf_json_get_f32(yyjson_val* obj,
                              const char* key,
                              float default_value,
                              float* out,
                              const char* err_path,
                              gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }

  if (!yyjson_is_num(v)) {
    gltf_set_err(out_err, "must be a number", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = (float)yyjson_get_num(v);
  return GLTF_OK;
}

gltf_result gltf_json_get_bool(yyjson_val* obj,
                               const char* key,
                               int default_value,
                               gltf_bool* out,
                               const char* err_path,
                               gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value ? 1u : 0u;
    return GLTF_OK;
  }

  if (!yyjson_is_bool(v)) {
    gltf_set_err(out_err, "must be boolean", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = yyjson_get_bool(v) ? GLTF_TRUE : GLTF_FALSE;
  return GLTF_OK;
}

gltf_result gltf_json_get_bool_u8(yyjson_val* obj,
                                  const char* key,
                                  int default_value,
                                  uint8_t* out,
                                  const char* err_path,
                                  gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = (uint8_t)(default_value ? 1u : 0u);
    return GLTF_OK;
  }

  if (!yyjson_is_bool(v)) {
    gltf_set_err(out_err, "must be boolean", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  *out = yyjson_get_bool(v) ? 1u : 0u;
  return GLTF_OK;
}

gltf_result gltf_json_get_material_alpha_mode(yyjson_val* obj,
                                              const char* key,
                                              gltf_alpha_mode default_value,
                                              gltf_alpha_mode* out,
                                              const char* err_path,
                                              gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  yyjson_val* v = yyjson_obj_get(obj, key);
  if (!v) {
    *out = default_value;
    return GLTF_OK;
  }

  if (!yyjson_is_str(v)) {
    gltf_set_err(out_err, "must be string", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

  const char* s = yyjson_get_str(v);
  if (strcmp(s, "OPAQUE") == 0) {
    *out = GLTF_ALPHA_OPAQUE;
  } else if (strcmp(s, "MASK") == 0) {
    *out = GLTF_ALPHA_MASK;
  } else if (strcmp(s, "BLEND") == 0) {
    *out = GLTF_ALPHA_BLEND;
  } else {
    gltf_set_err(out_err, "invalid alphaMode", err_path, 1, 1);
    return GLTF_ERR_PARSE;
  }

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

gltf_result gltf_json_get_str_opt_dup_arena_cstr(yyjson_val* obj,
                                                 const char* key,
                                                 gltf_arena* arena,
                                                 const char** out,
                                                 const char* err_path,
                                                 gltf_error* out_err) {
  if (!out) return GLTF_ERR_INVALID;

  gltf_str s = gltf_str_invalid();
  gltf_result r = gltf_json_get_str_opt_dup_arena(obj, key, arena, &s, err_path, out_err);
  if (r != GLTF_OK) return r;

  *out = arena_get_str(arena, s);
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
// Parsing utilities for gltf_doc.c
// ----------------------------------------------------------------------------

gltf_result gltf_parse_scenes(gltf_doc* doc,
                              yyjson_val* root,
                              gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.scenes", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* scenes_val = yyjson_obj_get(root, "scenes");
  if (!scenes_val) {
    doc->scene_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(scenes_val)) {
    gltf_set_err(out_err, "must be array", "root.scenes", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->scene_count = (unsigned)yyjson_arr_size(scenes_val);
  if (doc->scene_count == 0) return GLTF_OK;

  doc->scenes = (gltf_scene*)calloc(doc->scene_count, sizeof(gltf_scene));
  if (!doc->scenes) {
    gltf_set_err(out_err, "out of memory", "root.scenes", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t scene_idx, scene_max;
  yyjson_val* scene_val = NULL;
  yyjson_arr_foreach(scenes_val, scene_idx, scene_max, scene_val) {
    if (!yyjson_is_obj(scene_val)) {
      gltf_set_err(out_err, "must be object", "root.scenes[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_str_opt_dup_arena(
      scene_val,
      "name",
      &doc->arena,
      &doc->scenes[scene_idx].name,
      "root.scenes[].name",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32_index_array_range_opt(
      doc,
      scene_val,
      "nodes",
      &doc->scenes[scene_idx].nodes,
      "root.scenes[].nodes",
      "root.scenes[].nodes[]",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_nodes(gltf_doc* doc,
                             yyjson_val* root,
                             gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.nodes", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* nodes_val = yyjson_obj_get(root, "nodes");
  if (!nodes_val) {
    doc->node_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(nodes_val)) {
    gltf_set_err(out_err, "must be array", "root.nodes", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->node_count = (unsigned)yyjson_arr_size(nodes_val);
  if (doc->node_count == 0) return GLTF_OK;

  doc->nodes = (gltf_node*)calloc(doc->node_count, sizeof(gltf_node));
  if (!doc->nodes) {
    gltf_set_err(out_err, "out of memory", "root.nodes", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t node_idx, node_max;
  yyjson_val* node_val = NULL;
  yyjson_arr_foreach(nodes_val, node_idx, node_max, node_val) {
    if (!yyjson_is_obj(node_val)) {
      gltf_set_err(out_err, "must be object", "root.nodes[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_str_opt_dup_arena(
      node_val,
      "name",
      &doc->arena,
      &doc->nodes[node_idx].name,
      "root.nodes[].name",
      out_err);
    if (r != GLTF_OK) return r;

    yyjson_val* v = yyjson_obj_get(node_val, "matrix");
    doc->nodes[node_idx].has_matrix = v ? 1 : 0;

    r = gltf_json_get_mat4_f32_opt(
      node_val,
      "matrix",
      (float[16]){
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f,
        0.f, 0.f, 0.f, 1.f
      },
      doc->nodes[node_idx].matrix,
      "root.nodes[].matrix",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_vec3_f32_opt(
      node_val,
      "translation",
      (float[3]){ 0.f, 0.f, 0.f },
      doc->nodes[node_idx].translation,
      "root.nodes[].translation",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_vec4_f32_opt(
      node_val,
      "rotation",
      (float[4]){ 0.f, 0.f, 0.f, 1.f },
      doc->nodes[node_idx].rotation,
      "root.nodes[].rotation",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_vec3_f32_opt(
      node_val,
      "scale",
      (float[3]){ 1.f, 1.f, 1.f },
      doc->nodes[node_idx].scale,
      "root.nodes[].scale",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32_index_array_range_opt(
      doc,
      node_val,
      "children",
      &doc->nodes[node_idx].children,
      "root.nodes[].children",
      "root.nodes[].children[]",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      node_val,
      "mesh",
      -1,
      &doc->nodes[node_idx].mesh,
      "root.nodes[].mesh",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_meshes(gltf_doc* doc,
                              yyjson_val* root,
                              gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.meshes", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* meshes_val = yyjson_obj_get(root, "meshes");
  if (!meshes_val) {
    doc->mesh_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(meshes_val)) {
    gltf_set_err(out_err, "must be array", "root.meshes", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->mesh_count = (unsigned)yyjson_arr_size(meshes_val);
  if (doc->mesh_count == 0) return GLTF_OK;

  doc->meshes = (gltf_mesh*)calloc(doc->mesh_count, sizeof(gltf_mesh));
  if (!doc->meshes) {
    gltf_set_err(out_err, "out of memory", "root.meshes", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t mesh_idx, mesh_max;
  yyjson_val* mesh_val = NULL;
  yyjson_arr_foreach(meshes_val, mesh_idx, mesh_max, mesh_val) {
    if (!yyjson_is_obj(mesh_val)) {
      gltf_set_err(out_err, "must be object", "root.meshes[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_str_opt_dup_arena(
      mesh_val,
      "name",
      &doc->arena,
      &doc->meshes[mesh_idx].name,
      "root.meshes[].name",
      out_err);
    if (r != GLTF_OK) return r;

    uint32_t primitive_first = doc->primitive_count;
    uint32_t primitive_count = 0;

    yyjson_val* primitives_val = yyjson_obj_get(mesh_val, "primitives");
    if (!primitives_val) {
      gltf_set_err(out_err, "must be present", "root.meshes[].primitives", 1, 1);
      return GLTF_ERR_PARSE;
    } else if (!yyjson_is_arr(primitives_val)) {
      gltf_set_err(out_err, "must be array", "root.meshes[].primitives", 1, 1);
      return GLTF_ERR_PARSE;
    }

    size_t _primitive_count = yyjson_arr_size(primitives_val);
    if (_primitive_count > UINT32_MAX) {
      gltf_set_err(out_err, "too many primitives", "root.meshes[].primitives", 1, 1);
      return GLTF_ERR_PARSE;
    }
    primitive_count = (uint32_t)_primitive_count;

    if (primitive_count > 0) {
      size_t new_count = (size_t)doc->primitive_count + (size_t)primitive_count;

      if (new_count > SIZE_MAX / sizeof(gltf_primitive)) {
        gltf_set_err(out_err, "too many primitives", "root.meshes[].primitives", 1, 1);
        return GLTF_ERR_IO;
      }

      size_t prim_size = new_count * sizeof(gltf_primitive);
      gltf_primitive* primitives = (gltf_primitive*)realloc(doc->primitives, prim_size);
      if (!primitives) {
        gltf_set_err(out_err, "out of memory", "root.meshes[].primitives", 1, 1);
        return GLTF_ERR_IO;
      }
      memset(&primitives[doc->primitive_count], 0, primitive_count * sizeof(gltf_primitive));
      doc->primitives = primitives;

      size_t prim_idx, prim_max;
      yyjson_val* prim_val = NULL;
      yyjson_arr_foreach(primitives_val, prim_idx, prim_max, prim_val) {
        if (!yyjson_is_obj(prim_val)) {
          gltf_set_err(out_err, "must be object", "root.meshes[].primitives[]", 1, 1);
          return GLTF_ERR_PARSE;
        }

        yyjson_val* attributes_val = yyjson_obj_get(prim_val, "attributes");
        if (!attributes_val) {
          gltf_set_err(out_err, "must be present", "root.meshes[].primitives[].attributes", 1, 1);
          return GLTF_ERR_PARSE;
        }
        if (!yyjson_is_obj(attributes_val)) {
          gltf_set_err(out_err, "must be object", "root.meshes[].primitives[].attributes", 1, 1);
          return GLTF_ERR_PARSE;
        }

        uint32_t attr_first = doc->prim_attr_count;
        uint32_t attr_count = 0;

        // pass 1: count known attrs
        uint32_t known = 0;

        size_t attributes_idx, attributes_max;
        yyjson_val *key, *val;
        yyjson_obj_foreach(attributes_val, attributes_idx, attributes_max, key, val) {
          uint32_t set = 0;
          gltf_attr_semantic sem = gltf_parse_semantic(yyjson_get_str(key), &set);
          if (sem == GLTF_ATTR_UNKNOWN) continue;
          known++;
        }

        if (known > 0) {
          // reserve once
          size_t new_attr_count = (size_t)doc->prim_attr_count + (size_t)known;
          if (new_attr_count > SIZE_MAX / sizeof(gltf_prim_attr)) {
            gltf_set_err(out_err, "too many primitive attributes", "root.meshes[].primitives", 1, 1);
            return GLTF_ERR_IO;
          }
          size_t prim_attr_size = new_attr_count * sizeof(gltf_prim_attr);
          gltf_prim_attr* prim_attrs = (gltf_prim_attr*)realloc(doc->prim_attrs, prim_attr_size);
          if (!prim_attrs) {
            gltf_set_err(out_err, "out of memory", "root.meshes[].primitives", 1, 1);
            return GLTF_ERR_IO;
          }
          doc->prim_attrs = prim_attrs;

          yyjson_obj_foreach(attributes_val, attributes_idx, attributes_max, key, val) {
            uint32_t out_set_index;
            gltf_attr_semantic semantic = gltf_parse_semantic(
              yyjson_get_str(key),
              &out_set_index
            );

            if (semantic == GLTF_ATTR_UNKNOWN) {
              // Ignore unknown semantics.
              continue;
            }
            if (!yyjson_is_uint(val)) {
              gltf_set_err(out_err, "must be unsigned integer", "root.meshes[].primitives[].attributes[]", 1, 1);
              return GLTF_ERR_PARSE;
            }

            gltf_prim_attr attr;
            attr.semantic = semantic;
            attr.set_index = out_set_index;
            attr.accessor_index = (uint32_t)yyjson_get_uint(val);
            doc->prim_attrs[doc->prim_attr_count++] = attr;

            attr_count++;
          }
        }

        uint32_t primitive_idx = primitive_first + (uint32_t)prim_idx;

        r = gltf_json_get_i32(
          prim_val,
          "indices",
          -1,
          &doc->primitives[primitive_idx].indices_accessor,
          "root.meshes[].primitives[].indices",
          out_err);
        if (r != GLTF_OK) return r;

        int32_t mode_;
        r = gltf_json_get_i32(
          prim_val,
          "mode",
          GLTF_PRIM_TRIANGLES,
          &mode_,
          "root.meshes[].primitives[].mode",
          out_err);
        if (r != GLTF_OK) return r;
        if (mode_ < 0 || mode_ > 6) {
          gltf_set_err(out_err, "invalid primitive mode", "root.meshes[].primitives[].mode", 1, 1);
          return GLTF_ERR_PARSE;
        }
        doc->primitives[primitive_idx].mode = (gltf_prim_mode)mode_;

        doc->primitives[primitive_idx].attributes_first = attr_first;
        doc->primitives[primitive_idx].attributes_count = attr_count;
      }
    }

    doc->meshes[mesh_idx].primitive_first = primitive_first;
    doc->meshes[mesh_idx].primitive_count = primitive_count;
    doc->primitive_count += primitive_count;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_accessors(gltf_doc* doc,
                                 yyjson_val* root,
                                 gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.accessors", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* accessors_val = yyjson_obj_get(root, "accessors");
  if (!accessors_val) {
    doc->accessor_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(accessors_val)) {
    gltf_set_err(out_err, "must be array", "root.accessors", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->accessor_count = (unsigned)yyjson_arr_size(accessors_val);
  if (doc->accessor_count == 0) return GLTF_OK;

  doc->accessors = (gltf_accessor*)calloc(doc->accessor_count, sizeof(gltf_accessor));
  if (!doc->accessors) {
    gltf_set_err(out_err, "out of memory", "root.accessors", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t accessor_idx, accessor_max;
  yyjson_val* accessor_val = NULL;
  yyjson_arr_foreach(accessors_val, accessor_idx, accessor_max, accessor_val) {
    if (!yyjson_is_obj(accessor_val)) {
      gltf_set_err(out_err, "must be object", "root.accessors[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_i32(
      accessor_val,
      "bufferView",
      -1,
      &doc->accessors[accessor_idx].buffer_view,
      "root.accessors[].bufferView",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32(
      accessor_val,
      "byteOffset",
      0,
      &doc->accessors[accessor_idx].byte_offset,
      "root.accessors[].byteOffset",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32_req(
      accessor_val,
      "componentType",
      &doc->accessors[accessor_idx].component_type,
      "root.accessors[].componentType",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32_req(
      accessor_val,
      "count",
      &doc->accessors[accessor_idx].count,
      "root.accessors[].count",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_accessor_type_required(
      accessor_val,
      "type",
      &doc->accessors[accessor_idx].type,
      "root.accessors[].type",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_bool_u8(
      accessor_val,
      "normalized",
      0,
      &doc->accessors[accessor_idx].normalized,
      "root.accessors[].normalized",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_buffer_views(gltf_doc* doc,
                                    yyjson_val* root,
                                    gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.bufferViews", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* buffer_views_val = yyjson_obj_get(root, "bufferViews");
  if (!buffer_views_val) {
    doc->buffer_view_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(buffer_views_val)) {
    gltf_set_err(out_err, "must be array", "root.bufferViews", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->buffer_view_count = (unsigned)yyjson_arr_size(buffer_views_val);
  if (doc->buffer_view_count == 0) return GLTF_OK;

  doc->buffer_views = (gltf_buffer_view*)calloc(doc->buffer_view_count, sizeof(gltf_buffer_view));
  if (!doc->buffer_views) {
    gltf_set_err(out_err, "out of memory", "root.bufferViews", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t buffer_view_idx, buffer_view_max;
  yyjson_val* buffer_view_val = NULL;
  yyjson_arr_foreach(buffer_views_val, buffer_view_idx, buffer_view_max, buffer_view_val) {
    if (!yyjson_is_obj(buffer_view_val)) {
      gltf_set_err(out_err, "must be object", "root.bufferViews[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_u32_req(
      buffer_view_val,
      "buffer",
      &doc->buffer_views[buffer_view_idx].buffer,
      "root.bufferViews[].buffer",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32_req(
      buffer_view_val,
      "byteLength",
      &doc->buffer_views[buffer_view_idx].byte_length,
      "root.bufferViews[].byteLength",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32(
      buffer_view_val,
      "byteOffset",
      0,
      &doc->buffer_views[buffer_view_idx].byte_offset,
      "root.bufferViews[].byteOffset",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32(
      buffer_view_val,
      "byteStride",
      0,
      &doc->buffer_views[buffer_view_idx].byte_stride,
      "root.bufferViews[].byteStride",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_u32(
      buffer_view_val,
      "target",
      0,
      &doc->buffer_views[buffer_view_idx].target,
      "root.bufferViews[].target",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_buffers(gltf_doc* doc,
                               yyjson_val* root,
                               const char* doc_path,
                               gltf_error* out_err) {
  if (!doc || !root || !doc_path) {
    gltf_set_err(out_err, "invalid arguments", "root.buffers", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* buffers_val = yyjson_obj_get(root, "buffers");
  if (!buffers_val) {
    doc->buffer_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(buffers_val)) {
    gltf_set_err(out_err, "must be array", "root.buffers", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->buffer_count = (unsigned)yyjson_arr_size(buffers_val);
  if (doc->buffer_count == 0) return GLTF_OK;

  doc->buffers = (gltf_buffer*)calloc(doc->buffer_count, sizeof(gltf_buffer));
  if (!doc->buffers) {
    gltf_set_err(out_err, "out of memory", "root.buffers", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t buffer_idx, buffer_max;
  yyjson_val* buffer_val = NULL;
  yyjson_arr_foreach(buffers_val, buffer_idx, buffer_max, buffer_val) {
    if (!yyjson_is_obj(buffer_val)) {
      gltf_set_err(out_err, "must be object", "root.buffers[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_u32_req(
      buffer_val,
      "byteLength",
      &doc->buffers[buffer_idx].byte_length,
      "root.buffers[].byteLength",
      out_err);
    if (r != GLTF_OK) return r;

    yyjson_val* uri_val = yyjson_obj_get(buffer_val, "uri");
    if (!uri_val) {
      gltf_set_err(out_err, "must be present", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_PARSE;
    } else if (!yyjson_is_str(uri_val)) {
      gltf_set_err(out_err, "must be string", "root.buffers[].uri", 1, 1);
      return GLTF_ERR_PARSE;
    } else {
      const char* uri = yyjson_get_str(uri_val);
      gltf_str v = arena_strdup(&doc->arena, uri);
      if (!gltf_str_is_valid(v)) {
        gltf_set_err(out_err, "out of memory", "root.buffers[].uri", 1, 1);
        return GLTF_ERR_IO;
      }
      doc->buffers[buffer_idx].uri = v;

      if (strncmp(uri, "data:", 5) != 0) {
        // External file

        size_t dir_len = gltf_fs_dir_len(doc_path);
        char* full = gltf_fs_join_dir_leaf(doc_path, dir_len, uri);
        if (!full) {
          gltf_set_err(out_err, "out of memory", "root.buffers[].uri", 1, 1);
          return GLTF_ERR_IO;
        }

        uint32_t actual_len = 0;
        uint8_t* data = NULL;
        gltf_fs_status st = gltf_fs_read_file_exact_u32(full,
                                                        doc->buffers[buffer_idx].byte_length,
                                                        &data,
                                                        &actual_len);
        free(full);

        switch (st) {
        case GLTF_FS_OK:
          doc->buffers[buffer_idx].data = data;
          break;
        case GLTF_FS_SIZE_MISMATCH:
          gltf_set_err(out_err, "buffer file size does not match byteLength", "root.buffers[].byteLength", 1, 1);
          return GLTF_ERR_PARSE;
        case GLTF_FS_OOM:
          gltf_set_err(out_err, "out of memory", "root.buffers[].byteLength", 1, 1);
          return GLTF_ERR_IO;
        case GLTF_FS_TOO_LARGE:
          gltf_set_err(out_err, "buffer file too large", "root.buffers[].uri", 1, 1);
          return GLTF_ERR_PARSE;
        default:
          gltf_set_err(out_err, "failed to read buffer file", "root.buffers[].uri", 1, 1);
          return GLTF_ERR_IO;
        }
      } else {
        // Data URI (base64)

        uint8_t* bytes = NULL;
        uint32_t out_len = 0;
        r = gltf_decode_data_uri(uri, doc->buffers[buffer_idx].byte_length, &bytes, &out_len, out_err);
        if (r != GLTF_OK) return r;
        doc->buffers[buffer_idx].data = bytes;
      }
    }
  }

  return GLTF_OK;
}

gltf_result gltf_parse_images(gltf_doc* doc,
                              yyjson_val* root,
                              gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.images", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* images_val = yyjson_obj_get(root, "images");
  if (!images_val) {
    doc->image_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(images_val)) {
    gltf_set_err(out_err, "must be array", "root.images", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->image_count = (unsigned)yyjson_arr_size(images_val);
  if (doc->image_count == 0) return GLTF_OK;

  doc->images = (gltf_image*)calloc(doc->image_count, sizeof(gltf_image));
  if (!doc->images) {
    gltf_set_err(out_err, "out of memory", "root.images", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t image_idx, image_max;
  yyjson_val* image_val = NULL;
  yyjson_arr_foreach(images_val, image_idx, image_max, image_val) {
    if (!yyjson_is_obj(image_val)) {
      gltf_set_err(out_err, "must be object", "root.images[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_str_opt_dup_arena_cstr(
      image_val,
      "name",
      &doc->arena,
      &doc->images[image_idx].name,
      "root.images[].name",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_str_opt_dup_arena_cstr(
      image_val,
      "mimeType",
      &doc->arena,
      &doc->images[image_idx].mime_type,
      "root.images[].mimeType",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_str_opt_dup_arena_cstr(
      image_val,
      "uri",
      &doc->arena,
      &doc->images[image_idx].uri,
      "root.images[].uri",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      image_val,
      "bufferView",
      -1,
      &doc->images[image_idx].buffer_view,
      "root.images[].bufferView",
      out_err);
    if (r != GLTF_OK) return r;

    gltf_image* img = &doc->images[image_idx];

    // Determine image kind
    if (img->buffer_view >= 0) {
      img->kind = GLTF_IMAGE_BUFFER_VIEW;

      // Spec: mimeType is required when bufferView is used
      if (!img->mime_type) {
        gltf_set_err(out_err,
                     "mimeType is required when bufferView is used",
                     "root.images[].mimeType",
                     1, 1);
        return GLTF_ERR_PARSE;
      }
    } else if (img->uri) {
      // Check for data URI
      if (strncmp(img->uri, "data:", 5) == 0) {
        img->kind = GLTF_IMAGE_DATA_URI;
      } else {
        img->kind = GLTF_IMAGE_URI;
      }
    } else {
      img->kind = GLTF_IMAGE_NONE;
    }

    img->resolved = NULL;

    if (img->kind == GLTF_IMAGE_URI && img->uri) {
      if (gltf_path_is_relative(img->uri)) {
        const char* doc_dir = arena_get_str(&doc->arena, doc->doc_dir);
        size_t doc_dir_len = doc_dir ? strlen(doc_dir) : 0;

        char* joined = gltf_fs_join_dir_leaf(doc_dir, doc_dir_len, img->uri);
        if (joined) {
          gltf_str gs = arena_strdup(&doc->arena, joined);
          free(joined);
          if (gltf_str_is_valid(gs)) {
            img->resolved = arena_get_str(&doc->arena, gs);
          }
        }
      } else {
        img->resolved = img->uri;
      }
    }
  }

  return GLTF_OK;
}

gltf_result gltf_parse_samplers(gltf_doc* doc,
                                yyjson_val* root,
                                gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.samplers", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* samplers_val = yyjson_obj_get(root, "samplers");
  if (!samplers_val) {
    doc->sampler_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(samplers_val)) {
    gltf_set_err(out_err, "must be array", "root.samplers", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->sampler_count = (unsigned)yyjson_arr_size(samplers_val);
  if (doc->sampler_count == 0) return GLTF_OK;

  doc->samplers = (gltf_sampler*)calloc(doc->sampler_count, sizeof(gltf_sampler));
  if (!doc->samplers) {
    gltf_set_err(out_err, "out of memory", "root.samplers", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t sampler_idx, samplers_max;
  yyjson_val* sampler_val = NULL;
  yyjson_arr_foreach(samplers_val, sampler_idx, samplers_max, sampler_val) {
    if (!yyjson_is_obj(sampler_val)) {
      gltf_set_err(out_err, "must be object", "root.samplers[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_i32(
      sampler_val,
      "magFilter",
      -1,
      &doc->samplers[sampler_idx].mag_filter,
      "root.samplers[].magFilter",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      sampler_val,
      "minFilter",
      -1,
      &doc->samplers[sampler_idx].min_filter,
      "root.samplers[].minFilter",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      sampler_val,
      "wrapS",
      10497,
      &doc->samplers[sampler_idx].wrap_s,
      "root.samplers[].wrapS",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      sampler_val,
      "wrapT",
      10497,
      &doc->samplers[sampler_idx].wrap_t,
      "root.samplers[].wrapT",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_textures(gltf_doc* doc,
                                yyjson_val* root,
                                gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.textures", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* textures_val = yyjson_obj_get(root, "textures");
  if (!textures_val) {
    doc->texture_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(textures_val)) {
    gltf_set_err(out_err, "must be array", "root.textures", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->texture_count = (unsigned)yyjson_arr_size(textures_val);
  if (doc->texture_count == 0) return GLTF_OK;

  doc->textures = (gltf_texture*)calloc(doc->texture_count, sizeof(gltf_texture));
  if (!doc->textures) {
    gltf_set_err(out_err, "out of memory", "root.textures", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t texture_idx, textures_max;
  yyjson_val* texture_val = NULL;
  yyjson_arr_foreach(textures_val, texture_idx, textures_max, texture_val) {
    if (!yyjson_is_obj(texture_val)) {
      gltf_set_err(out_err, "must be object", "root.textures[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_i32(
      texture_val,
      "sampler",
      -1,
      &doc->textures[texture_idx].sampler,
      "root.textures[].sampler",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_i32(
      texture_val,
      "source",
      -1,
      &doc->textures[texture_idx].source,
      "root.textures[].source",
      out_err);
    if (r != GLTF_OK) return r;
  }

  return GLTF_OK;
}

gltf_result gltf_parse_materials(gltf_doc* doc,
                                 yyjson_val* root,
                                 gltf_error* out_err) {
  if (!doc || !root) {
    gltf_set_err(out_err, "invalid arguments", "root.materials", 1, 1);
    return GLTF_ERR_INVALID;
  }

  yyjson_val* materials_val = yyjson_obj_get(root, "materials");
  if (!materials_val) {
    doc->material_count = 0;
    return GLTF_OK;
  }
  if (!yyjson_is_arr(materials_val)) {
    gltf_set_err(out_err, "must be array", "root.materials", 1, 1);
    return GLTF_ERR_PARSE;
  }

  doc->material_count = (unsigned)yyjson_arr_size(materials_val);
  if (doc->material_count == 0) return GLTF_OK;

  doc->materials = (gltf_material*)calloc(doc->material_count, sizeof(gltf_material));
  if (!doc->materials) {
    gltf_set_err(out_err, "out of memory", "root.materials", 1, 1);
    return GLTF_ERR_IO;
  }

  size_t material_idx, material_max;
  yyjson_val* material_val = NULL;
  yyjson_arr_foreach(materials_val, material_idx, material_max, material_val) {
    if (!yyjson_is_obj(material_val)) {
      gltf_set_err(out_err, "must be object", "root.materials[]", 1, 1);
      return GLTF_ERR_PARSE;
    }

    gltf_result r = gltf_json_get_str_opt_dup_arena_cstr(
      material_val,
      "name",
      &doc->arena,
      &doc->materials[material_idx].name,
      "root.materials[].name",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_bool(
      material_val,
      "doubleSided",
      0,
      &doc->materials[material_idx].double_sided,
      "root.materials[].doubleSided",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_material_alpha_mode(
      material_val,
      "alphaMode",
      GLTF_ALPHA_OPAQUE,
      &doc->materials[material_idx].alpha_mode,
      "root.materials[].alphaMode",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_f32(
      material_val,
      "alphaCutoff",
      .5f,
      &doc->materials[material_idx].alpha_cutoff,
      "root.materials[].alphaCutoff",
      out_err);
    if (r != GLTF_OK) return r;

    r = gltf_json_get_vec3_f32_opt(
      material_val,
      "emissiveFactor",
      (float[3]){ 0.f, 0.f, 0.f },
      doc->materials[material_idx].emissive_factor,
      "root.materials[].emissiveFactor",
      out_err);
    if (r != GLTF_OK) return r;

    // materials[i].emissiveTexture
    yyjson_val* emissive_tex_val = yyjson_obj_get(material_val, "emissiveTexture");
    if (emissive_tex_val) {
      if (!yyjson_is_obj(emissive_tex_val)) {
        gltf_set_err(out_err, "must be object", "root.materials[].emissiveTexture", 1, 1);
        return GLTF_ERR_PARSE;
      }

      yyjson_val* index_val = yyjson_obj_get(emissive_tex_val, "index");
      if (!index_val) {
        gltf_set_err(out_err, "must be present", "root.materials[].emissiveTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        emissive_tex_val,
        "index",
        -1,
        &doc->materials[material_idx].emissive_texture.index,
        "root.materials[].emissiveTexture.index",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].emissive_texture.index < 0) {
        gltf_set_err(out_err, "index out of range", "root.materials[].emissiveTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        emissive_tex_val,
        "texCoord",
        0,
        &doc->materials[material_idx].emissive_texture.tex_coord,
        "root.materials[].emissiveTexture.texCoord",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].emissive_texture.tex_coord < 0) {
        gltf_set_err(out_err, "texCoord out of range", "root.materials[].emissiveTexture.texCoord", 1, 1);
        return GLTF_ERR_PARSE;
      }
    } else {
      doc->materials[material_idx].emissive_texture.index = -1;
      doc->materials[material_idx].emissive_texture.tex_coord = 0;
    }

    // materials[i].normalTexture
    doc->materials[material_idx].normal_texture.base.index = -1;
    doc->materials[material_idx].normal_texture.base.tex_coord = 0;
    doc->materials[material_idx].normal_texture.scale = 1.f;

    yyjson_val* normal_tex_val = yyjson_obj_get(material_val, "normalTexture");
    if (normal_tex_val) {
      if (!yyjson_is_obj(normal_tex_val)) {
        gltf_set_err(out_err, "must be object", "root.materials[].normalTexture", 1, 1);
        return GLTF_ERR_PARSE;
      }

      yyjson_val* index_val = yyjson_obj_get(normal_tex_val, "index");
      if (!index_val) {
        gltf_set_err(out_err, "must be present", "root.materials[].normalTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        normal_tex_val,
        "index",
        -1,
        &doc->materials[material_idx].normal_texture.base.index,
        "root.materials[].normalTexture.index",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].normal_texture.base.index < 0) {
        gltf_set_err(out_err, "index out of range", "root.materials[].normalTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        normal_tex_val,
        "texCoord",
        0,
        &doc->materials[material_idx].normal_texture.base.tex_coord,
        "root.materials[].normalTexture.texCoord",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].normal_texture.base.tex_coord < 0) {
        gltf_set_err(out_err, "texCoord out of range", "root.materials[].normalTexture.texCoord", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_f32(
        normal_tex_val,
        "scale",
        1.f,
        &doc->materials[material_idx].normal_texture.scale,
        "root.materials[].normalTexture.scale",
        out_err);
      if (r != GLTF_OK) return r;
    }

    // materials[i].occlusionTexture
    doc->materials[material_idx].occlusion_texture.base.index = -1;
    doc->materials[material_idx].occlusion_texture.base.tex_coord = 0;
    doc->materials[material_idx].occlusion_texture.strength = 1.f;

    yyjson_val* occlusion_tex_val = yyjson_obj_get(material_val, "occlusionTexture");
    if (occlusion_tex_val) {
      if (!yyjson_is_obj(occlusion_tex_val)) {
        gltf_set_err(out_err, "must be object", "root.materials[].occlusionTexture", 1, 1);
        return GLTF_ERR_PARSE;
      }

      yyjson_val* index_val = yyjson_obj_get(occlusion_tex_val, "index");
      if (!index_val) {
        gltf_set_err(out_err, "must be present", "root.materials[].occlusionTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        occlusion_tex_val,
        "index",
        -1,
        &doc->materials[material_idx].occlusion_texture.base.index,
        "root.materials[].occlusionTexture.index",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].occlusion_texture.base.index < 0) {
        gltf_set_err(out_err, "index out of range", "root.materials[].occlusionTexture.index", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_i32(
        occlusion_tex_val,
        "texCoord",
        0,
        &doc->materials[material_idx].occlusion_texture.base.tex_coord,
        "root.materials[].occlusionTexture.texCoord",
        out_err);
      if (r != GLTF_OK) return r;
      if (doc->materials[material_idx].occlusion_texture.base.tex_coord < 0) {
        gltf_set_err(out_err, "texCoord out of range", "root.materials[].occlusionTexture.texCoord", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_f32(
        occlusion_tex_val,
        "strength",
        1.f,
        &doc->materials[material_idx].occlusion_texture.strength,
        "root.materials[].occlusionTexture.strength",
        out_err);
      if (r != GLTF_OK) return r;
    }

    // materials[i].pbrMetallicRoughness
    doc->materials[material_idx].pbr.base_color_factor[0] = 1.f;
    doc->materials[material_idx].pbr.base_color_factor[1] = 1.f;
    doc->materials[material_idx].pbr.base_color_factor[2] = 1.f;
    doc->materials[material_idx].pbr.base_color_factor[3] = 1.f;
    doc->materials[material_idx].pbr.base_color_texture.index = -1;
    doc->materials[material_idx].pbr.base_color_texture.tex_coord = 0;
    doc->materials[material_idx].pbr.metallic_factor = 1.f;
    doc->materials[material_idx].pbr.roughness_factor = 1.f;
    doc->materials[material_idx].pbr.metallic_roughness_texture.index = -1;
    doc->materials[material_idx].pbr.metallic_roughness_texture.tex_coord = 0;

    yyjson_val* pbr_val = yyjson_obj_get(material_val, "pbrMetallicRoughness");
    if (pbr_val) {
      if (!yyjson_is_obj(pbr_val)) {
        gltf_set_err(out_err, "must be object", "root.materials[].pbrMetallicRoughness", 1, 1);
        return GLTF_ERR_PARSE;
      }

      r = gltf_json_get_vec4_f32_opt(
        pbr_val,
        "baseColorFactor",
        (float[4]){ 1.f, 1.f, 1.f, 1.f },
        doc->materials[material_idx].pbr.base_color_factor,
        "root.materials[].pbrMetallicRoughness.baseColorFactor",
        out_err);
      if (r != GLTF_OK) return r;

      r = gltf_json_get_f32(
        pbr_val,
        "metallicFactor",
        1.f,
        &doc->materials[material_idx].pbr.metallic_factor,
        "root.materials[].pbrMetallicRoughness.metallicFactor",
        out_err);
      if (r != GLTF_OK) return r;

      r = gltf_json_get_f32(
        pbr_val,
        "roughnessFactor",
        1.f,
        &doc->materials[material_idx].pbr.roughness_factor,
        "root.materials[].pbrMetallicRoughness.roughnessFactor",
        out_err);
      if (r != GLTF_OK) return r;

      yyjson_val* base_color_tex_val = yyjson_obj_get(pbr_val, "baseColorTexture");
      if (base_color_tex_val) {
        if (!yyjson_is_obj(base_color_tex_val)) {
          gltf_set_err(out_err, "must be object", "root.materials[].pbrMetallicRoughness.baseColorTexture", 1, 1);
          return GLTF_ERR_PARSE;
        }

        yyjson_val* index_val = yyjson_obj_get(base_color_tex_val, "index");
        if (!index_val) {
          gltf_set_err(out_err, "must be present", "root.materials[].pbrMetallicRoughness.baseColorTexture.index", 1, 1);
          return GLTF_ERR_PARSE;
        }

        r = gltf_json_get_i32(
          base_color_tex_val,
          "index",
          -1,
          &doc->materials[material_idx].pbr.base_color_texture.index,
          "root.materials[].pbrMetallicRoughness.baseColorTexture.index",
          out_err);
        if (r != GLTF_OK) return r;
        if (doc->materials[material_idx].pbr.base_color_texture.index < 0) {
          gltf_set_err(out_err, "index out of range", "root.materials[].pbrMetallicRoughness.baseColorTexture.index", 1, 1);
          return GLTF_ERR_PARSE;
        }

        r = gltf_json_get_i32(
          base_color_tex_val,
          "texCoord",
          0,
          &doc->materials[material_idx].pbr.base_color_texture.tex_coord,
          "root.materials[].pbrMetallicRoughness.baseColorTexture.texCoord",
          out_err);
        if (r != GLTF_OK) return r;
        if (doc->materials[material_idx].pbr.base_color_texture.tex_coord < 0) {
          gltf_set_err(out_err, "texCoord out of range", "root.materials[].pbrMetallicRoughness.baseColorTexture.texCoord", 1, 1);
          return GLTF_ERR_PARSE;
        }
      }

      yyjson_val* mr_tex_val = yyjson_obj_get(pbr_val, "metallicRoughnessTexture");
      if (mr_tex_val) {
        if (!yyjson_is_obj(mr_tex_val)) {
          gltf_set_err(out_err, "must be object", "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture", 1, 1);
          return GLTF_ERR_PARSE;
        }

        yyjson_val* index_val = yyjson_obj_get(mr_tex_val, "index");
        if (!index_val) {
          gltf_set_err(out_err, "must be present", "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture.index", 1, 1);
          return GLTF_ERR_PARSE;
        }

        r = gltf_json_get_i32(
          mr_tex_val,
          "index",
          -1,
          &doc->materials[material_idx].pbr.metallic_roughness_texture.index,
          "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture.index",
          out_err);
        if (r != GLTF_OK) return r;
        if (doc->materials[material_idx].pbr.metallic_roughness_texture.index < 0) {
          gltf_set_err(out_err, "index out of range", "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture.index", 1, 1);
          return GLTF_ERR_PARSE;
        }

        r = gltf_json_get_i32(
          mr_tex_val,
          "texCoord",
          0,
          &doc->materials[material_idx].pbr.metallic_roughness_texture.tex_coord,
          "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture.texCoord",
          out_err);
        if (r != GLTF_OK) return r;
        if (doc->materials[material_idx].pbr.metallic_roughness_texture.tex_coord < 0) {
          gltf_set_err(out_err, "texCoord out of range", "root.materials[].pbrMetallicRoughness.metallicRoughnessTexture.texCoord", 1, 1);
          return GLTF_ERR_PARSE;
        }
      }
    }
  }

  return GLTF_OK;
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
