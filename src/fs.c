#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef enum gltf_fs_status {
  GLTF_FS_OK = 0,
  GLTF_FS_INVALID,
  GLTF_FS_IO,
  GLTF_FS_OOM,
  GLTF_FS_SIZE_MISMATCH,
  GLTF_FS_TOO_LARGE
} gltf_fs_status;

static int gltf_fs_is_abs(const char* path) {
  if (!path || path[0] == '\0') return 0;
#ifdef _WIN32
  // "C:\", "C:/"
  if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) return 1;
  // UNC paths: "\\server\share"
  if (path[0] == '\\' && path[1] == '\\') return 1;
  // treat "/foo" as absolute too (MSVCRT accepts it)
  if (path[0] == '/') return 1;
  return 0;
#else
  return path[0] == '/';
#endif
}

// Returns number of bytes in directory prefix INCLUDING the last separator.
// Example: "/a/b/c.gltf" -> 5 ("/a/b/")
//          "c.gltf"      -> 0
size_t gltf_fs_dir_len(const char* path) {
  if (!path) return 0;
  size_t n = strlen(path);
  while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) n--; // trim trailing separators
  for (size_t i = n; i > 0; i--) {
    char c = path[i - 1];
    if (c == '/' || c == '\\') return i; // include separator
  }
  return 0;
}

// Joins dir_prefix[0..dir_len) + leaf into malloc'd string.
// If leaf is absolute, returns a copy of leaf (ignores dir).
char* gltf_fs_join_dir_leaf(const char* dir_prefix, size_t dir_len, const char* leaf) {
  if (!leaf) return NULL;

  if (gltf_fs_is_abs(leaf) || !dir_prefix || dir_len == 0) {
    size_t n = strlen(leaf);
    char* s = (char*)malloc(n + 1);
    if (!s) return NULL;
    memcpy(s, leaf, n + 1);
    return s;
  }

  size_t leaf_len = strlen(leaf);

  // Ensure exactly one separator between dir and leaf:
  // your dir_len already "includes separator", so we just concatenate.
  if (dir_len > SIZE_MAX - (leaf_len + 1)) return NULL;
  size_t total = dir_len + leaf_len + 1;

  char* s = (char*)malloc(total);
  if (!s) return NULL;
  memcpy(s, dir_prefix, dir_len);
  memcpy(s + dir_len, leaf, leaf_len + 1);
  return s;
}

// Reads exactly expected_len bytes from file. Allocates *out_data with malloc.
// If expected_len==0: reads the entire file (size determined by ftell).
gltf_fs_status gltf_fs_read_file_exact_u32(const char* path,
                                           uint32_t expected_len,
                                           uint8_t** out_data,
                                           uint32_t* out_len) {
  if (!path || !out_data) return GLTF_FS_INVALID;
  *out_data = NULL;
  if (out_len) *out_len = 0;

  FILE* f = fopen(path, "rb");
  if (!f) return GLTF_FS_IO;

  // Determine size
  if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return GLTF_FS_IO; }
  long szl = ftell(f);
  if (szl < 0) { fclose(f); return GLTF_FS_IO; }
  if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return GLTF_FS_IO; }

  uint64_t sz = (uint64_t)szl;
  if (sz > UINT32_MAX) { fclose(f); return GLTF_FS_TOO_LARGE; }

  uint32_t file_len = (uint32_t)sz;

  if (expected_len != 0 && file_len != expected_len) {
    fclose(f);
    return GLTF_FS_SIZE_MISMATCH;
  }

  uint32_t want = (expected_len != 0) ? expected_len : file_len;

  uint8_t* data = NULL;
  if (want > 0) {
    data = (uint8_t*)malloc((size_t)want);
    if (!data) { fclose(f); return GLTF_FS_OOM; }

    size_t nread = fread(data, 1, (size_t)want, f);
    if (nread != (size_t)want) {
      free(data);
      fclose(f);
      return GLTF_FS_IO;
    }
  }

  fclose(f);
  *out_data = data;
  if (out_len) *out_len = want;
  return GLTF_FS_OK;
}
