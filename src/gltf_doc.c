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

typedef enum gltf_fs_status {
  GLTF_FS_OK = 0,
  GLTF_FS_INVALID,
  GLTF_FS_IO,
  GLTF_FS_OOM,
  GLTF_FS_SIZE_MISMATCH,
  GLTF_FS_TOO_LARGE,
  GLTF_FS_BAD_ARGUMENT
} gltf_fs_status;

size_t gltf_fs_dir_len(const char* path);

gltf_fs_status gltf_fs_read_file(const char* path, uint8_t** out_data, size_t* out_size);


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

static gltf_result gltf_load_json_string_ex(uint8_t* json_text,
                                            uint32_t json_len,
                                            const gltf_load_context* ctx,
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
  yyjson_doc* json_doc = NULL;
  yyjson_read_err err;
  yyjson_read_flag flg = 0;

  if (out_doc) {
    *out_doc = NULL;
  }

  if (!json_text || !ctx || !out_doc) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  json_doc = yyjson_read_opts((char*)json_text, json_len, flg, NULL, &err);
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

  if (!(ctx->flags & GLTF_LOAD_CTX_GLB) && ctx->doc_dir) {
    // init doc_dir (directory of the .gltf file)
    GLTF_TRY(gltf_init_doc_dir(doc, ctx->doc_dir, out_err));
  }

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
  GLTF_TRY(gltf_parse_buffers(doc, root, ctx, out_err));

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


static int gltf_is_glb_bytes(const uint8_t* data, size_t size) {
  if (!data || size < 12) return 0;
  return rd_u32_le(data + 0) == 0x46546C67u; // 'glTF'
}

gltf_result gltf_load_file(const char* path,
                           gltf_doc** out_doc,
                           gltf_error* out_err) {
  if (out_doc) {
    *out_doc = NULL;
  }

  if (!path || !out_doc || !out_err) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  uint8_t* data = NULL;
  size_t size = 0;
  int st = gltf_fs_read_file(path, &data, &size);
  if (st != GLTF_FS_OK) {
    gltf_set_err(out_err, "failed to read file", path, 1, 1);
    return GLTF_ERR_IO;
  }

  gltf_result rc;

  // GLB
  if (gltf_is_glb_bytes(data, size)) {
    rc = gltf_load_glb_bytes(data, size, out_doc, out_err);
    free(data);
    return rc;
  }

  // glTF JSON
  gltf_load_context ctx = {0};

  // directory for external resources
  size_t dir_len = gltf_fs_dir_len(path);
  if (dir_len > 0) {
    char* dir = (char*)malloc(dir_len + 1);
    if (!dir) {
      free(data);
      gltf_set_err(out_err, "out of memory", path, 1, 1);
      return GLTF_ERR_IO;
    }
    memcpy(dir, path, dir_len);
    dir[dir_len] = '\0';
    ctx.doc_dir = dir;
  }

  rc = gltf_load_json_string_ex(data, (uint32_t)size, &ctx, out_doc, out_err);

  free((void*)ctx.doc_dir);
  free(data);
  return rc;
}

gltf_result gltf_load_json_string(uint8_t* json_text,
                                  uint32_t json_len,
                                  gltf_doc** out_doc,
                                  gltf_error* out_err) {
  gltf_load_context ctx = {0};
  return gltf_load_json_string_ex(json_text, json_len, &ctx, out_doc, out_err);
}

gltf_result gltf_load_glb_bytes(const uint8_t* data,
                                size_t size,
                                gltf_doc** out_doc,
                                gltf_error* out_err) {
  if (!data || !out_doc || !out_err) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  *out_doc = NULL;

  if (size < 12) {
    gltf_set_err(out_err, "file too small", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  // read GLB header (little-endian)
  uint32_t magic   = rd_u32_le(data + 0);
  uint32_t version = rd_u32_le(data + 4);
  uint32_t length  = rd_u32_le(data + 8);

  if (magic != 0x46546C67u) { // 'glTF'
    gltf_set_err(out_err, "bad magic", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  if (version != 2u) {
    gltf_set_err(out_err, "unsupported glb version", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  if ((size_t)length != size) {
    gltf_set_err(out_err, "glb length mismatch", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  size_t offset = 12;

  const uint8_t* json_ptr = NULL;
  uint32_t json_len = 0;

  const uint8_t* bin_ptr = NULL;
  uint32_t bin_len = 0;
  
  int seen_any_chunk = 0;
  int json_was_first = 0;
  
  // iterate chunks
  while (offset < size) {
    if (size - offset < 8) {
      gltf_set_err(out_err, "truncated chunk header", "root", 1, 1);
      return GLTF_ERR_INVALID;
    }

    const uint32_t chunk_len  = rd_u32_le(data + offset + 0);
    const uint32_t chunk_type = rd_u32_le(data + offset + 4);
    offset += 8;

    if (chunk_len > size - offset) {
      gltf_set_err(out_err, "chunk out of bounds", "root", 1, 1);
      return GLTF_ERR_INVALID;
    }

    // GLB chunks are 4-byte aligned; chunk_len should be multiple of 4
    if ((chunk_len & 3u) != 0u) {
      gltf_set_err(out_err, "chunk length not 4-byte aligned", "root", 1, 1);
      return GLTF_ERR_INVALID;
    }

    const uint8_t* payload_ptr = data + offset;

    if (!seen_any_chunk) {
      seen_any_chunk = 1;
      json_was_first = (chunk_type == 0x4E4F534Au); // 'JSON'
    }

    if (chunk_type == 0x4E4F534Au) { // 'JSON'
      if (json_ptr != NULL) {
        gltf_set_err(out_err, "duplicate JSON chunk", "root", 1, 1);
        return GLTF_ERR_INVALID;
      }
      json_ptr = payload_ptr;
      json_len = chunk_len;
    }
    else if (chunk_type == 0x004E4942u) { // 'BIN\0'
      if (bin_ptr != NULL) {
        gltf_set_err(out_err, "duplicate BIN chunk", "root", 1, 1);
        return GLTF_ERR_INVALID;
      }
      bin_ptr = payload_ptr;
      bin_len = chunk_len;
    } else {
      // unknown chunk type: ignore
    }
    
    offset += (size_t)chunk_len;
  }

  // validate presence/order
  if (!json_ptr || json_len == 0) {
    gltf_set_err(out_err, "missing JSON chunk", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  // optional strictness: JSON must be the first chunk
  if (!json_was_first) {
    gltf_set_err(out_err, "JSON chunk must be first", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  // copy JSON to NUL-terminated string
  uint8_t* json_text = (uint8_t*)malloc((size_t)(json_len + 1u));
  if (!json_text) {
    gltf_set_err(out_err, "out of memory", "root", 1, 1);
    return GLTF_ERR_IO;
  }
  memcpy(json_text, json_ptr, json_len);
  json_text[json_len] = '\0';

  gltf_doc* doc = NULL;
  gltf_load_context ctx = {
    .internal_bin      = bin_ptr,
    .internal_bin_size = (bin_ptr && bin_len) ? bin_len : 0,
    .doc_dir           = NULL,
    .flags             = GLTF_LOAD_CTX_GLB,
  };
  gltf_result rc = gltf_load_json_string_ex(json_text, json_len, &ctx, &doc, out_err);

  free(json_text);
  json_text = NULL;

  if (rc != GLTF_OK) {
    return rc;
  }

  *out_doc = doc;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;
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
