#include "unity.h"

#include "gltf/gltf.h"

#include <stdio.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/06-datauri.gltf";
}

void test_06_images_datauri(void) {
#if GLTF_ENABLE_IMAGES
  const char* path = sample_path();

  gltf_error err = {0};

  int rc = gltf_load_file(path, &g_doc, &err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_load_file('%s') failed rc=%d msg=%s path=%s line=%d col=%d",
                   path,
                   rc,
                   err.message ? err.message : "(null)",
                   err.path ? err.path : "(null)",
                   err.line,
                   err.col);
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_NOT_NULL(g_doc);

  uint32_t image_count = gltf_doc_image_count(g_doc);
  TEST_ASSERT_EQUAL_UINT32(1u, image_count);

  gltf_image_pixels pixels = {0};
  rc = gltf_image_decode_rgba8(g_doc, 0u, &pixels, &err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_image_decode_rgba8 failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   err.message ? err.message : "(null)",
                   err.path ? err.path : "(null)",
                   err.line,
                   err.col);
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_NOT_NULL(pixels.pixels);
  TEST_ASSERT_EQUAL_UINT32(1u, pixels.width);
  TEST_ASSERT_EQUAL_UINT32(1u, pixels.height);

  // Expect a single red pixel (RGBA = 255, 0, 0, 255).
  TEST_ASSERT_EQUAL_UINT8(255u, pixels.pixels[0]);
  TEST_ASSERT_EQUAL_UINT8(0u, pixels.pixels[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, pixels.pixels[2]);
  TEST_ASSERT_EQUAL_UINT8(255u, pixels.pixels[3]);

  gltf_image_pixels_free(&pixels);
  gltf_free(g_doc);
  g_doc = NULL;
#else
  TEST_IGNORE_MESSAGE("GLTF_ENABLE_IMAGES is OFF");
#endif
}
