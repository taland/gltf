// examples/07_glb_to_gltf/main.c
//
// Usage:
//   07_glb_to_gltf <input.glb> <output_base>
//
// Produces:
//   <output_base>.gltf
//   <output_base>.bin
//
// Notes:
// - This example unpacks GLB container directly (header + chunks).
// - It rewrites JSON so buffers[0].uri points to "<output_base>.bin".
// - It also updates buffers[0].byteLength to BIN chunk length (strict).
//
// Build integration depends on your repo setup (yyjson is used here).

#include "gltf/gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "yyjson.h" // used by the example to patch JSON reliably

// ------------------------- small fs helpers -------------------------

static int read_file_all(const char* path, uint8_t** out_data, size_t* out_size) {
  *out_data = NULL;
  *out_size = 0;

  FILE* f = fopen(path, "rb");
  if (!f) return 0;

  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
  long n = ftell(f);
  if (n < 0) { fclose(f); return 0; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

  uint8_t* data = (uint8_t*)malloc((size_t)n);
  if (!data) { fclose(f); return 0; }

  size_t got = fread(data, 1, (size_t)n, f);
  fclose(f);

  if (got != (size_t)n) { free(data); return 0; }

  *out_data = data;
  *out_size = (size_t)n;
  return 1;
}

static int write_file_all(const char* path, const uint8_t* data, size_t size) {
  FILE* f = fopen(path, "wb");
  if (!f) return 0;
  size_t wr = fwrite(data, 1, size, f);
  fclose(f);
  return wr == size;
}

static uint32_t rd_u32_le(const uint8_t* p) {
  return (uint32_t)p[0]
       | ((uint32_t)p[1] << 8u)
       | ((uint32_t)p[2] << 16u)
       | ((uint32_t)p[3] << 24u);
}

// ------------------------- GLB unpack -------------------------

typedef struct glb_view {
  const uint8_t* json_ptr;
  uint32_t       json_len;
  const uint8_t* bin_ptr;
  uint32_t       bin_len;
} glb_view;

static int glb_unpack(const uint8_t* data, size_t size, glb_view* out, char* err, size_t err_cap) {
  memset(out, 0, sizeof(*out));

  if (!data || size < 12) {
    snprintf(err, err_cap, "file too small");
    return 0;
  }

  const uint32_t magic   = rd_u32_le(data + 0);
  const uint32_t version = rd_u32_le(data + 4);
  const uint32_t length  = rd_u32_le(data + 8);

  if (magic != 0x46546C67u) { // 'glTF'
    snprintf(err, err_cap, "bad magic");
    return 0;
  }
  if (version != 2u) {
    snprintf(err, err_cap, "unsupported glb version: %u", version);
    return 0;
  }
  if ((size_t)length != size) {
    snprintf(err, err_cap, "length mismatch header=%u actual=%zu", length, size);
    return 0;
  }

  size_t offset = 12;
  int seen_any_chunk = 0;
  int json_was_first = 0;

  while (offset < size) {
    if (size - offset < 8) {
      snprintf(err, err_cap, "truncated chunk header");
      return 0;
    }

    const uint32_t chunk_len  = rd_u32_le(data + offset + 0);
    const uint32_t chunk_type = rd_u32_le(data + offset + 4);
    offset += 8;

    if ((chunk_len & 3u) != 0u) {
      snprintf(err, err_cap, "chunk length not 4-byte aligned");
      return 0;
    }
    if ((size_t)chunk_len > size - offset) {
      snprintf(err, err_cap, "chunk out of bounds");
      return 0;
    }

    const uint8_t* payload = data + offset;

    if (!seen_any_chunk) {
      seen_any_chunk = 1;
      json_was_first = (chunk_type == 0x4E4F534Au); // 'JSON'
    }

    if (chunk_type == 0x4E4F534Au) { // JSON
      if (out->json_ptr) {
        snprintf(err, err_cap, "duplicate JSON chunk");
        return 0;
      }
      out->json_ptr = payload;
      out->json_len = chunk_len;
    } else if (chunk_type == 0x004E4942u) { // 'BIN\0'
      if (out->bin_ptr) {
        snprintf(err, err_cap, "duplicate BIN chunk");
        return 0;
      }
      out->bin_ptr = payload;
      out->bin_len = chunk_len;
    } else {
      // ignore unknown chunk
    }

    offset += (size_t)chunk_len;
  }

  if (!out->json_ptr || out->json_len == 0) {
    snprintf(err, err_cap, "missing JSON chunk");
    return 0;
  }
  if (!json_was_first) {
    snprintf(err, err_cap, "JSON chunk must be first");
    return 0;
  }

  return 1;
}

// ------------------------- JSON patch: buffers[0].uri + byteLength -------------------------

static int patch_gltf_json_set_uri_and_length(const uint8_t* json_ptr,
                                              uint32_t json_len,
                                              const char* bin_filename,
                                              uint32_t bin_len,
                                              uint8_t** out_json,
                                              size_t* out_json_len,
                                              char* err,
                                              size_t err_cap) {
  *out_json = NULL;
  *out_json_len = 0;

  yyjson_read_err rerr;
  yyjson_doc* doc = yyjson_read_opts((char*)json_ptr, json_len, 0, NULL, &rerr);
  if (!doc) {
    snprintf(err, err_cap, "json parse error: %s", rerr.msg ? rerr.msg : "(null)");
    return 0;
  }

  yyjson_mut_doc* mdoc = yyjson_mut_doc_new(NULL);
  yyjson_mut_val* root = yyjson_val_mut_copy(mdoc, yyjson_doc_get_root(doc));
  yyjson_mut_doc_set_root(mdoc, root);

  if (!root || !yyjson_mut_is_obj(root)) {
    yyjson_doc_free(doc);
    yyjson_mut_doc_free(mdoc);
    snprintf(err, err_cap, "root must be object");
    return 0;
  }

  yyjson_mut_val* buffers = yyjson_mut_obj_get(root, "buffers");
  if (!buffers || !yyjson_mut_is_arr(buffers) || yyjson_mut_arr_size(buffers) == 0) {
    yyjson_doc_free(doc);
    yyjson_mut_doc_free(mdoc);
    snprintf(err, err_cap, "root.buffers must be non-empty array");
    return 0;
  }

  yyjson_mut_val* buf0 = yyjson_mut_arr_get(buffers, 0);
  if (!buf0 || !yyjson_mut_is_obj(buf0)) {
    yyjson_doc_free(doc);
    yyjson_mut_doc_free(mdoc);
    snprintf(err, err_cap, "root.buffers[0] must be object");
    return 0;
  }

  // Set/overwrite uri
  yyjson_mut_obj_put(buf0,
                     yyjson_mut_str(mdoc, "uri"),
                     yyjson_mut_strcpy(mdoc, bin_filename));

  // Set/overwrite byteLength to match BIN chunk length (strict)
  yyjson_mut_obj_put(buf0,
                     yyjson_mut_str(mdoc, "byteLength"),
                     yyjson_mut_uint(mdoc, (uint64_t)bin_len));

  // Write pretty JSON
  yyjson_write_err werr;
  size_t out_len = 0;
  char* out_txt = yyjson_mut_write_opts(mdoc, YYJSON_WRITE_PRETTY, NULL, &out_len, &werr);
  if (!out_txt) {
    yyjson_doc_free(doc);
    yyjson_mut_doc_free(mdoc);
    snprintf(err, err_cap, "json write error: %s", werr.msg ? werr.msg : "(null)");
    return 0;
  }

  *out_json = (uint8_t*)out_txt;
  *out_json_len = out_len;

  yyjson_doc_free(doc);
  yyjson_mut_doc_free(mdoc);
  return 1;
}

// ------------------------- main -------------------------

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <input.glb> <output_base>\n", argv[0]);
    return 2;
  }

  const char* in_path = argv[1];
  const char* out_base = argv[2];

  char out_gltf[1024];
  char out_bin[1024];
  snprintf(out_gltf, sizeof(out_gltf), "%s.gltf", out_base);
  snprintf(out_bin, sizeof(out_bin), "%s.bin", out_base);

  uint8_t* glb_data = NULL;
  size_t glb_size = 0;

  if (!read_file_all(in_path, &glb_data, &glb_size)) {
    fprintf(stderr, "Failed to read: %s\n", in_path);
    return 1;
  }

  // optional: validate your loader
  {
    gltf_doc* doc = NULL;
    gltf_error err = {0};
    gltf_result rc = gltf_load_glb_bytes(glb_data, glb_size, &doc, &err);
    if (rc != GLTF_OK) {
      fprintf(stderr, "gltf_load_glb_bytes failed rc=%d msg=%s path=%s\n",
              rc,
              err.message ? err.message : "(null)",
              err.path ? err.path : "(null)");
      free(glb_data);
      return 1;
    }
    gltf_free(doc);
  }

  glb_view v;
  char perr[256];
  if (!glb_unpack(glb_data, glb_size, &v, perr, sizeof(perr))) {
    fprintf(stderr, "GLB parse error: %s\n", perr);
    free(glb_data);
    return 1;
  }

  // write BIN
  if (v.bin_ptr && v.bin_len > 0) {
    if (!write_file_all(out_bin, v.bin_ptr, (size_t)v.bin_len)) {
      fprintf(stderr, "Failed to write: %s\n", out_bin);
      free(glb_data);
      return 1;
    }
  } else {
    // valid GLB may omit BIN; we still write JSON and skip .bin
    fprintf(stderr, "Warning: GLB has no BIN chunk, writing only .gltf\n");
  }

  // patch JSON -> .gltf (set buffers[0].uri to "<base>.bin", update byteLength)
  uint8_t* patched_json = NULL;
  size_t patched_len = 0;

  const char* bin_leaf = strrchr(out_bin, '/');
  bin_leaf = bin_leaf ? (bin_leaf + 1) : out_bin;

  char jerr[256];
  if (!patch_gltf_json_set_uri_and_length(v.json_ptr,
                                          v.json_len,
                                          bin_leaf,
                                          (v.bin_ptr && v.bin_len) ? v.bin_len : 0,
                                          &patched_json,
                                          &patched_len,
                                          jerr,
                                          sizeof(jerr))) {
    fprintf(stderr, "JSON patch error: %s\n", jerr);
    free(glb_data);
    return 1;
  }

  if (!write_file_all(out_gltf, patched_json, patched_len)) {
    fprintf(stderr, "Failed to write: %s\n", out_gltf);
    free(patched_json);
    free(glb_data);
    return 1;
  }

  free(patched_json);
  free(glb_data);

  printf("OK: wrote %s\n", out_gltf);
  if (v.bin_ptr && v.bin_len > 0) {
    printf("OK: wrote %s\n", out_bin);
  }
  return 0;
}
