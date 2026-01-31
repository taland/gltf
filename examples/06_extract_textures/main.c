#include "gltf/gltf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

static void die(const char* msg) {
  fprintf(stderr, "%s\n", msg);
  exit(1);
}

static int ensure_dir(const char* path) {
  if (!path || path[0] == '\0') return 0;

#ifdef _WIN32
  int rc = _mkdir(path);
#else
  int rc = mkdir(path, 0755);
#endif

  if (rc == 0) return 1;
  if (errno == EEXIST) return 1;
  return 0;
}

static int dir_exists(const char* path) {
  if (!path || path[0] == '\0') return 0;
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
}

typedef struct app_args {
  const char* path;
  const char* out_dir;
  int force;
} app_args;

static app_args parse_args(int argc, char** argv) {
  app_args a;
  a.path = NULL;
  a.out_dir = "out";
  a.force = 0;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <model.gltf|model.glb> [out_dir] [--force]\n", argv[0]);
    exit(1);
  }

  if (argc >= 2) a.path = argv[1];
  if (argc >= 3) a.out_dir = argv[2];

  for (int i = 2; i < argc; ++i) {
    if (strcmp(argv[i], "--force") == 0) {
      a.force = 1;
    }
  }

  return a;
}

int main(int argc, char** argv) {
  app_args args = parse_args(argc, argv);

  if (args.force) {
    if (!ensure_dir(args.out_dir)) {
      fprintf(stderr, "failed to create output dir: %s\n", args.out_dir);
      return 1;
    }
  } else if (!dir_exists(args.out_dir)) {
    fprintf(stderr, "output dir does not exist: %s (use --force to create)\n", args.out_dir);
    return 1;
  }

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result r = gltf_load_file(args.path, &doc, &err);
  if (r != GLTF_OK) {
    fprintf(stderr, "load failed: %s\n", err.message);
    return 1;
  }

  uint32_t image_count = gltf_doc_image_count(doc);
  printf("images: %u\n", image_count);

  for (uint32_t i = 0; i < image_count; ++i) {
    gltf_image_pixels pixels = {0};

    r = gltf_image_decode_rgba8(doc, i, &pixels, &err);
    if (r != GLTF_OK) {
      fprintf(stderr, "image[%u] decode failed: %s\n", i, err.message);
      continue;
    }

    char path_buf[512];
    snprintf(path_buf, sizeof(path_buf),
             "%s/image_%02u_%ux%u.png",
             args.out_dir, i, pixels.width, pixels.height);

    r = gltf_write_png_rgba8(path_buf,
                             pixels.width,
                             pixels.height,
                             pixels.pixels,
                             &err);
    if (r != GLTF_OK) {
      fprintf(stderr, "image[%u] write failed: %s\n", i, err.message);
      gltf_image_pixels_free(&pixels);
      continue;
    }

    printf("image[%u]: %ux%u -> %s\n", i, pixels.width, pixels.height, path_buf);

    gltf_image_pixels_free(&pixels);
  }

  gltf_free(doc);
  return 0;
}
