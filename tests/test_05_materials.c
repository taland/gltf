#include "unity.h"

#include "gltf/gltf.h"

#include <stdio.h>
#include <string.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/05-materials.gltf";
}

void test_05_materials(void) {
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

  // Materials
  uint32_t material_count = gltf_doc_material_count(g_doc);
  TEST_ASSERT_EQUAL_UINT32(1u, material_count);

  const gltf_material* mat = NULL;
  TEST_ASSERT_TRUE(gltf_doc_material(g_doc, 0u, &mat));
  TEST_ASSERT_NOT_NULL(mat);
  TEST_ASSERT_NOT_NULL(mat->name);
  TEST_ASSERT_EQUAL_STRING("Material.001", mat->name);
  TEST_ASSERT_EQUAL_UINT8(GLTF_TRUE, mat->double_sided);
  TEST_ASSERT_EQUAL_INT(GLTF_ALPHA_OPAQUE, mat->alpha_mode);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, mat->alpha_cutoff);

  TEST_ASSERT_EQUAL_INT(0, mat->normal_texture.base.index);
  TEST_ASSERT_EQUAL_INT(0, mat->normal_texture.base.tex_coord);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->normal_texture.scale);

  TEST_ASSERT_EQUAL_INT(-1, mat->occlusion_texture.base.index);
  TEST_ASSERT_EQUAL_INT(0, mat->occlusion_texture.base.tex_coord);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->occlusion_texture.strength);

  TEST_ASSERT_EQUAL_INT(-1, mat->emissive_texture.index);
  TEST_ASSERT_EQUAL_INT(0, mat->emissive_texture.tex_coord);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, mat->emissive_factor[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, mat->emissive_factor[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, mat->emissive_factor[2]);

  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.base_color_factor[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.base_color_factor[1]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.base_color_factor[2]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.base_color_factor[3]);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.metallic_factor);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, mat->pbr.roughness_factor);

  TEST_ASSERT_EQUAL_INT(1, mat->pbr.base_color_texture.index);
  TEST_ASSERT_EQUAL_INT(0, mat->pbr.base_color_texture.tex_coord);
  TEST_ASSERT_EQUAL_INT(2, mat->pbr.metallic_roughness_texture.index);
  TEST_ASSERT_EQUAL_INT(0, mat->pbr.metallic_roughness_texture.tex_coord);

  // Textures
  uint32_t texture_count = gltf_doc_texture_count(g_doc);
  TEST_ASSERT_EQUAL_UINT32(3u, texture_count);
  for (uint32_t i = 0; i < texture_count; i++) {
    const gltf_texture* tex = NULL;
    TEST_ASSERT_TRUE(gltf_doc_texture(g_doc, i, &tex));
    TEST_ASSERT_NOT_NULL(tex);
    TEST_ASSERT_EQUAL_INT(0, tex->sampler);
    TEST_ASSERT_EQUAL_INT((int32_t)i, tex->source);
  }

  // Images
  uint32_t image_count = gltf_doc_image_count(g_doc);
  TEST_ASSERT_EQUAL_UINT32(3u, image_count);
  for (uint32_t i = 0; i < image_count; i++) {
    const gltf_image* img = NULL;
    TEST_ASSERT_TRUE(gltf_doc_image(g_doc, i, &img));
    TEST_ASSERT_NOT_NULL(img);
    TEST_ASSERT_EQUAL_INT(GLTF_IMAGE_URI, img->kind);
    TEST_ASSERT_NOT_NULL(img->uri);
    TEST_ASSERT_EQUAL_INT(-1, img->buffer_view);
    if (img->kind == GLTF_IMAGE_BUFFER_VIEW) {
      TEST_ASSERT_NOT_NULL(img->mime_type);
    }

    const char* resolved = gltf_image_resolved_uri(g_doc, i);
    TEST_ASSERT_NOT_NULL(resolved);
  }

  // Samplers
  uint32_t sampler_count = gltf_doc_sampler_count(g_doc);
  TEST_ASSERT_EQUAL_UINT32(1u, sampler_count);
  const gltf_sampler* samp = NULL;
  TEST_ASSERT_TRUE(gltf_doc_sampler(g_doc, 0u, &samp));
  TEST_ASSERT_NOT_NULL(samp);
  TEST_ASSERT_EQUAL_INT(9729, samp->mag_filter);
  TEST_ASSERT_EQUAL_INT(9987, samp->min_filter);
  TEST_ASSERT_EQUAL_INT(10497, samp->wrap_s);
  TEST_ASSERT_EQUAL_INT(10497, samp->wrap_t);

  // Out-of-range checks
  TEST_ASSERT_FALSE(gltf_doc_material(g_doc, material_count, &mat));
}
