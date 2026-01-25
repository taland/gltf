// Minimal educational glTF 2.0 loader (C11).
//
// This module provides small file system helpers used by the loader.
//
// Responsibilities:
//   - compute a directory prefix length from a path
//   - join a base directory and leaf path into a malloc()'d string
//   - read an entire file (or an expected number of bytes)
//
// Notes:
//   - These helpers are internal; public API contracts live in include/gltf/gltf.h.
//   - Paths accept both '/' and '\\' as separators; Windows absolute paths are
//     recognized when built with _WIN32.


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


// ----------------------------------------------------------------------------
// Paths
// ----------------------------------------------------------------------------

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

// Returns the directory prefix length in bytes, including the trailing separator.
//
// Notes:
//   - Trailing separators are trimmed before scanning.
//   - Both '/' and '\\' are treated as separators.
size_t gltf_fs_dir_len(const char* path) {
  if (!path) return 0;
  size_t n = strlen(path);
  while (n > 0 && (path[n - 1] == '/' || path[n - 1] == '\\')) n--; // trim
  for (size_t i = n; i > 0; i--) {
    char c = path[i - 1];
    if (c == '/' || c == '\\') return i; // include separator
  }
  return 0;
}

// Joins dir_prefix[0..dir_len) and leaf into a malloc()'d path.
//
// Notes:
//   - If leaf is absolute, returns a copy of leaf (dir_prefix is ignored).
//   - dir_len is expected to include the trailing separator.
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

  // dir_len already includes the trailing separator.
  if (dir_len > SIZE_MAX - (leaf_len + 1)) return NULL;
  size_t total = dir_len + leaf_len + 1;

  char* s = (char*)malloc(total);
  if (!s) return NULL;
  memcpy(s, dir_prefix, dir_len);
  memcpy(s + dir_len, leaf, leaf_len + 1);
  return s;
}

// Reads a file into a malloc()'d buffer.
//
// Notes:
//   - If expected_len is non-zero, the file size must match exactly.
//   - If expected_len is zero, reads the entire file.
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

// Returns 1 if path is filesystem-relative, 0 otherwise.
//
// Non-relative paths include:
//   - absolute POSIX paths (/foo/bar)
//   - Windows absolute paths (C:\foo, C:/foo)
//   - Windows UNC paths (\\server\share)
//   - URIs with schemes (data:, http:, file:, etc.)
int gltf_path_is_relative(const char* path) {
  if (!path || !path[0]) return 1;

  // Windows UNC path "\\server\share"
  if (path[0] == '\\' && path[1] == '\\') return 0;

  // data: and other schemes are not filesystem-relative
  if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) {
    // scheme: "something:"
    const char* p = path;
    while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
           (*p >= '0' && *p <= '9') || *p == '+' || *p == '-' || *p == '.') {
      p++;
    }
    if (*p == ':') {
      return 0;
    }
  }

  // Absolute POSIX path
  if (path[0] == '/') return 0;

  // Windows absolute path "C:\"
  if (((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    return 0;
  }

  return 1;
}
