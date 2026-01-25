// Minimal educational glTF 2.0 loader (C11).
//
// This module owns document lifetime and top-level JSON parsing.
//
// Responsibilities:
//   - load and validate a .gltf JSON document
//   - populate document-owned arrays (scenes/nodes/meshes/primitives/...)
//   - load buffers from external files and data: URIs
//   - free all document-owned memory
//
// Notes:
//   - Public API contracts are documented in include/gltf/gltf.h.
//   - Error reporting uses static literals for message/path.


#include "gltf_internal.h"



// ----------------------------------------------------------------------------
// Internal helpers (fs)
// ----------------------------------------------------------------------------

size_t gltf_fs_dir_len(const char* path);


// ----------------------------------------------------------------------------
// Document lifetime
// ----------------------------------------------------------------------------

// Writes error details (null-safe).

void gltf_set_err(gltf_error* out_err,
                  const char* message,
                  const char* path,
                  int line, int col) {
  if (!out_err) return;
  out_err->message = message;
  out_err->path = path;
  out_err->line = line;
  out_err->col = col;
}

void gltf_set_err_if(gltf_error* out_err,
                     const char* message,
                     const char* path,
                     int line, int col) {
  if (out_err) {
    gltf_set_err(out_err, message, path, line, col);
  }
}

static gltf_result gltf_init_doc_dir(gltf_doc* doc, const char* path, gltf_error* out_err) {
  if (!doc || !path) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  doc->doc_dir = gltf_str_invalid();
  size_t dir_len = gltf_fs_dir_len(path);
  if (dir_len == 0) return GLTF_OK;

  char* tmp = (char*)malloc(dir_len + 1);
  if (!tmp) {
    gltf_set_err(out_err, "out of memory", "root", 1, 1);
    return GLTF_ERR_IO;
  }
  memcpy(tmp, path, dir_len);
  tmp[dir_len] = '\0';

  gltf_str s = arena_strdup(&doc->arena, tmp);
  free(tmp);
  if (!gltf_str_is_valid(s)) {
    gltf_set_err(out_err, "out of memory", "root", 1, 1);
    return GLTF_ERR_IO;
  }
  doc->doc_dir = s;
  return GLTF_OK;
}

gltf_result gltf_load_file(const char* path,
                           gltf_doc** out_doc,
                           gltf_error* out_err) {
#define GLTF_FAIL(_r, ...)               \
  do {                                   \
    gltf_set_err(out_err, __VA_ARGS__);  \
    yyjson_doc_free(json_doc);           \
    gltf_free(doc);                      \
    return _r;                           \
  } while (0)

#define GLTF_TRY(expr)                   \
  do {                                   \
    gltf_result _r = (expr);             \
    if (_r != GLTF_OK) {                 \
      yyjson_doc_free(json_doc);         \
      gltf_free(doc);                    \
      return _r;                         \
    }                                    \
  } while (0)

  gltf_doc* doc = NULL;
  yyjson_read_err err;
  yyjson_doc* json_doc = NULL;

  if (out_doc) {
    *out_doc = NULL;
  }

  if (!path || !out_doc) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  json_doc = yyjson_read_file(path, 0, NULL, &err);
  if (!json_doc) {
    GLTF_FAIL(GLTF_ERR_PARSE, err.msg, "root", 1, 1);
  }

  doc = (gltf_doc*)calloc(1, sizeof(gltf_doc));
  if (!doc) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  arena_init(&doc->arena, GLTF_ARENA_INITIAL_CAPACITY);
  if (!doc->arena.data) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  // init doc_dir (directory of the .gltf file)
  GLTF_TRY(gltf_init_doc_dir(doc, path, out_err));

  // Root
  yyjson_val* root = yyjson_doc_get_root(json_doc);
  if (!root || !yyjson_is_obj(root)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root", 1, 1);
  }

  // Default scene
  GLTF_TRY(
    gltf_json_get_i32(
      root,
      "scene",
      GLTF_DOC_DEFAULT_SCENE_INVALID,
      &doc->default_scene,
      "root.scene",
      out_err)
  );

  // Scenes
  GLTF_TRY(gltf_parse_scenes(doc, root, out_err));

  // Nodes
  GLTF_TRY(gltf_parse_nodes(doc, root, out_err));

  // Meshes
  GLTF_TRY(gltf_parse_meshes(doc, root, out_err));

  // Accessors
  GLTF_TRY(gltf_parse_accessors(doc, root, out_err));

  // BufferViews
  GLTF_TRY(gltf_parse_buffer_views(doc, root, out_err));

  // Buffers
  GLTF_TRY(gltf_parse_buffers(doc, root, path, out_err));

  // Images
  GLTF_TRY(gltf_parse_images(doc, root, out_err));
  
  // Samplers
  GLTF_TRY(gltf_parse_samplers(doc, root, out_err));

  // Textures
  GLTF_TRY(gltf_parse_textures(doc, root, out_err));

  // Materials
  GLTF_TRY(gltf_parse_materials(doc, root, out_err));

  yyjson_val* asset_val = yyjson_obj_get(root, "asset");
  if (!asset_val || !yyjson_is_obj(asset_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and an object", "root.asset", 1, 1);
  }

  yyjson_val* version_val = yyjson_obj_get(asset_val, "version");
  if (!version_val || !yyjson_is_str(version_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and a string", "root.asset.version", 1, 1);
  }

  const char* version = yyjson_get_str(version_val);
  if (strlen(version) >= sizeof(doc->asset_version)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "too long", "root.asset.version", 1, 1);
  }
  (void)strncpy(doc->asset_version, version, sizeof(doc->asset_version) - 1);
  doc->asset_version[sizeof(doc->asset_version) - 1] = '\0';

  yyjson_val* generator_val = yyjson_obj_get(asset_val, "generator");
  if (generator_val && yyjson_is_str(generator_val)) {
    const char* generator = yyjson_get_str(generator_val);
    doc->asset_generator = arena_strdup(&doc->arena, generator);
    if (!gltf_str_is_valid(doc->asset_generator)) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.asset.generator", 1, 1);
    }
  }

  yyjson_doc_free(json_doc);
  json_doc = NULL;

  *out_doc = doc;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;

#undef GLTF_TRY
#undef GLTF_FAIL
}

void gltf_free(gltf_doc* doc) {
  if (doc) {
    free(doc->scenes);
    free(doc->nodes);
    free(doc->meshes);
    free(doc->primitives);
    free(doc->prim_attrs);
    if (doc->buffers) {
      for (uint32_t i = 0; i < doc->buffer_count; i++) {
        free(doc->buffers[i].data);
      }
    }
    free(doc->buffers);
    free(doc->buffer_views);
    free(doc->materials);
    free(doc->textures);
    free(doc->images);
    free(doc->samplers);
    free(doc->accessors);
    free(doc->indices_u32);
    free(doc->arena.data);
    free(doc);
  }
}
