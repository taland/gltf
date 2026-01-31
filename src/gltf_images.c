// Minimal educational glTF 2.0 loader (C11).
//
// This module provides image loading/decoding utilities used by examples
// and optional texture workflows.
//
// Responsibilities:
//   - resolve image sources (URI, data URI, bufferView)
//   - decode PNG/JPEG bytes into RGBA8 via stb_image
//   - write RGBA8 buffers to PNG via stb_image_write
//
// Notes:
//   - Public API contracts live in include/gltf/gltf.h.
//   - Image decoding is optional and controlled by GLTF_ENABLE_IMAGES.
//   - Error paths are passed in by the caller to keep messages consistent.


#include "gltf_internal.h"

#if GLTF_ENABLE_IMAGES
  #define STB_IMAGE_IMPLEMENTATION
  #define STB_IMAGE_WRITE_IMPLEMENTATION
  #include "stb_image.h"
  #include "stb_image_write.h"
#endif


// Temporary byte buffer used while resolving image sources.
//
// Ownership:
//   - owned = 1 => data is heap-allocated and must be freed by gltf_blob_free()
//   - owned = 0 => data points into doc-owned memory (must not be freed)
typedef struct gltf_blob {
  const uint8_t* data;
  size_t size;
  int owned;
} gltf_blob;


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

gltf_fs_status gltf_fs_read_file_exact_u32(const char* path,
                                           uint32_t expected_len,
                                           uint8_t** out_data,
                                           uint32_t* out_len);


// ----------------------------------------------------------------------------
// Internal helpers (math / safety)
// ----------------------------------------------------------------------------

// helper: safe mul (returns 0 on overflow)
static int mul_size_t(size_t a, size_t b, size_t* out) {
    if (a == 0 || b == 0) { *out = 0; return 1; }
    if (a > (SIZE_MAX / b)) return 0;
    *out = a * b;
    return 1;
}

// Extracts the base64 payload pointer from a data URI.
// Returns NULL if the URI is not a base64 data URI.
static const char* gltf_data_uri_base64_payload(const char* uri) {
  // Expected: data:<mime>;base64,<payload>
  // We only need the part after ";base64,".
  if (!uri) return NULL;
  const char prefix[] = "data:";
  if (strncmp(uri, prefix, sizeof(prefix) - 1) != 0) return NULL;

  const char* marker = strstr(uri, ";base64,");
  if (!marker) return NULL;

  return marker + 8; // strlen(";base64,") == 8
}

// Resolves an image reference into compressed bytes (PNG/JPEG/etc.).
// On success, fills out_blob with bytes and ownership flag.
// On failure, out_blob is not modified.
static gltf_result gltf_image_load_bytes(const gltf_doc* doc,
                                          uint32_t image_index,
                                          gltf_blob* out_blob,
                                          gltf_error* out_err) {
  if (!doc || !out_blob) {
    gltf_set_err(out_err, "invalid args", NULL, 0, 0);
    return GLTF_ERR_INVALID;
  }

  if (image_index >= doc->image_count) {
    gltf_set_err(out_err, "image_index out of range", NULL, 0, 0);
    return GLTF_ERR_RANGE;
  }

  const gltf_image* img = &doc->images[image_index];

  gltf_blob b = {0};

  switch (img->kind) {
    case GLTF_IMAGE_URI: {
      const char* path = img->resolved ? img->resolved : img->uri;
      if (!path) {
        gltf_set_err(out_err, "image uri missing", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      uint8_t* data = NULL;
      uint32_t size_u32 = 0;

      gltf_fs_status fs = gltf_fs_read_file_exact_u32(path, 0, &data, &size_u32);
      if (fs != GLTF_FS_OK) {
        switch (fs) {
          case GLTF_FS_INVALID:
            gltf_set_err(out_err, "invalid image path", path, 0, 0);
            return GLTF_ERR_INVALID;
          case GLTF_FS_OOM:
            gltf_set_err(out_err, "out of memory reading image file", path, 0, 0);
            return GLTF_ERR_IO;
          case GLTF_FS_TOO_LARGE:
            gltf_set_err(out_err, "image file too large", path, 0, 0);
            return GLTF_ERR_IO;
          case GLTF_FS_IO:
          case GLTF_FS_SIZE_MISMATCH:
          default:
            gltf_set_err(out_err, "failed to read image file", path, 0, 0);
            return GLTF_ERR_IO;
        }
      }

      b.data = data;
      b.size = (size_t)size_u32;
      b.owned = 1;
      break;
    }

    case GLTF_IMAGE_DATA_URI: {
      if (!img->uri) {
        gltf_set_err(out_err, "data uri missing", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      const char* payload = gltf_data_uri_base64_payload(img->uri);
      if (!payload) {
        gltf_set_err(out_err, "invalid data uri (expected ;base64,)", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      const size_t payload_len = strlen(payload);

      size_t cap = gltf_base64_max_decoded_size(payload_len);
      if (cap == SIZE_MAX) {
        gltf_set_err(out_err, "data uri payload too large", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      uint8_t* decoded = NULL;
      if (cap > 0) {
        decoded = (uint8_t*)malloc(cap);
        if (!decoded) {
          gltf_set_err(out_err, "out of memory allocating decoded image", NULL, 0, 0);
          return GLTF_ERR_IO;
        }
      }

      size_t decoded_size = 0;
      int ok = gltf_base64_decode(payload, payload_len, decoded, cap, &decoded_size);
      if (!ok) {
        free(decoded);
        gltf_set_err(out_err, "base64 decode failed", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      b.data = decoded;
      b.size = decoded_size;
      b.owned = 1;
      break;
    }

    case GLTF_IMAGE_BUFFER_VIEW: {
      if (img->buffer_view < 0) {
        gltf_set_err(out_err, "image bufferView missing", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }
      const uint32_t bv_i = (uint32_t)img->buffer_view;

      if (bv_i >= doc->buffer_view_count) {
        gltf_set_err(out_err, "image bufferView out of range", NULL, 0, 0);
        return GLTF_ERR_RANGE;
      }

      const gltf_buffer_view* bv = &doc->buffer_views[bv_i];

      if (bv->buffer >= doc->buffer_count) {
        gltf_set_err(out_err, "bufferView.buffer out of range", NULL, 0, 0);
        return GLTF_ERR_RANGE;
      }

      const gltf_buffer* buf = &doc->buffers[bv->buffer];
      if (!buf->data) {
        gltf_set_err(out_err, "buffer data not loaded", NULL, 0, 0);
        return GLTF_ERR_PARSE;
      }

      b.data = (const uint8_t*)buf->data + (size_t)bv->byte_offset;
      b.size = (size_t)bv->byte_length;
      b.owned = 0;

      break;
    }

    default:
      gltf_set_err(out_err, "unsupported image kind", NULL, 0, 0);
      return GLTF_ERR_UNSUPPORTED;
  }

  *out_blob = b;
  return GLTF_OK;
}

// Releases a blob if it owns its memory and clears the fields.
static void gltf_blob_free(gltf_blob* b) {
  if (!b) return;
  if (b->owned && b->data) {
    free((void*)b->data);
  }
  b->data = NULL;
  b->size = 0;
  b->owned = 0;
}


// Decode pipeline for gltf_image_decode_rgba8():
//
// glTF image reference
//         |
//         v
// gltf_image_load_bytes()
//         |
//         v
// +---------------------+
// | gltf_blob           |   <-- compressed bytes (PNG/JPEG)
// +---------------------+
//         |
//         v
// stb_image decode
//         |
//         v
// +---------------------+
// | gltf_image_pixels   |   <-- RGBA8 pixels
// +---------------------+
//
// Decodes doc.images[image_index] into RGBA8 pixels.
// On success, fills out_pixels (caller owns pixels buffer).
// On failure, out_pixels is not modified.
gltf_result gltf_image_decode_rgba8(const gltf_doc* doc,
                                   uint32_t image_index,
                                   gltf_image_pixels* out_pixels,
                                   gltf_error* out_err) {
#if GLTF_ENABLE_IMAGES
  if (!doc || !out_pixels) {
    gltf_set_err(out_err, "invalid args", NULL, 0, 0);
    return GLTF_ERR_INVALID;
  }
  if (image_index >= doc->image_count) {
    gltf_set_err(out_err, "image_index out of range", NULL, 0, 0);
    return GLTF_ERR_RANGE;
  }

  // load compressed bytes (PNG/JPEG/...) into memory
  gltf_blob blob = {0};
  gltf_result r = gltf_image_load_bytes(doc, image_index, &blob, out_err);
  if (r != GLTF_OK) {
    gltf_blob_free(&blob);
    return r;
  }

  // decode to RGBA8 using stb_image
  int w = 0, h = 0, comp = 0;
  unsigned char* tmp = stbi_load_from_memory(
      (const unsigned char*)blob.data,
      (int)blob.size,
      &w, &h, &comp,
      4 // force RGBA
  );
  gltf_blob_free(&blob);

  if (!tmp || w <= 0 || h <= 0) {
    gltf_set_err(out_err, "image decode failed", NULL, 0, 0);
    if (tmp) stbi_image_free(tmp);
    return GLTF_ERR_PARSE;
  }

  // allocate output buffer (caller-owned via free)
  size_t bytes = 0, wh = 0;
  if (!mul_size_t((size_t)w, (size_t)h, &wh) || !mul_size_t(wh, 4u, &bytes)) {
    stbi_image_free(tmp);
    gltf_set_err(out_err, "image too large", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  uint8_t* pixels = (uint8_t*)malloc(bytes);
  if (!pixels) {
    stbi_image_free(tmp);
    gltf_set_err(out_err, "out of memory", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  memcpy(pixels, tmp, bytes);
  stbi_image_free(tmp);

  // commit (so out_pixels not modified on failure above)
  gltf_image_pixels out = {0};
  out.format = GLTF_PIXEL_RGBA8;
  out.width = (uint32_t)w;
  out.height = (uint32_t)h;
  out.stride_bytes = (uint32_t)w * 4u;
  out.pixels = pixels;

  *out_pixels = out;
  return GLTF_OK;
#else
  (void)doc;
  (void)image_index;
  (void)out_pixels;
  gltf_set_err(out_err, "images: not compiled in", NULL, 0, 0);
  return GLTF_ERR_UNSUPPORTED;
#endif
}

// Writes an RGBA8 pixel buffer to a PNG file on disk.
// On success, returns GLTF_OK.
// On failure, returns GLTF_ERR_* and fills out_err if provided.
gltf_result gltf_write_png_rgba8(const char* path,
                                uint32_t width,
                                uint32_t height,
                                const uint8_t* rgba_pixels,
                                gltf_error* out_err) {
#if GLTF_ENABLE_IMAGES
  if (!path || !rgba_pixels || width == 0 || height == 0) {
    gltf_set_err(out_err, "invalid args", NULL, 0, 0);
    return GLTF_ERR_INVALID;
  }

  size_t stride = 0;
  if (!mul_size_t((size_t)width, 4u, &stride)) {
    gltf_set_err(out_err, "image too large", NULL, 0, 0);
    return GLTF_ERR_IO;
  }

  int ok = stbi_write_png(path,
                          (int)width,
                          (int)height,
                          4,
                          (const void*)rgba_pixels,
                          (int)stride);
  if (!ok) {
    gltf_set_err(out_err, "failed to write png", path, 0, 0);
    return GLTF_ERR_IO;
  }

  return GLTF_OK;
#else
  gltf_set_err(out_err, "images: not complied in", 0, 0, 0);
  return GLTF_ERR_UNSUPPORTED;
#endif
}

// Frees pixels allocated by gltf_image_decode_rgba8().
// Safe to call with NULL or with p->pixels == NULL.
void gltf_image_pixels_free(gltf_image_pixels* p) {
  if (!p) return;
  free(p->pixels);
  p->pixels = NULL;
  p->width = p->height = p->stride_bytes = 0;
  p->format = 0;
}
