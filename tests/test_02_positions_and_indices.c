#include "unity.h"

#include "gltf/gltf.h"

#include <stdio.h>
#include <math.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/02_plane.gltf";
}

static const char* sample_path_embedded(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/02_plane_embedded.gltf";
}

static int feq(float a, float b) { return fabsf(a - b) < 1e-6f; }

static void assert_vec3_eq(const float* v, float x, float y, float z) {
  TEST_ASSERT_TRUE(feq(v[0], x));
  TEST_ASSERT_TRUE(feq(v[1], y));
  TEST_ASSERT_TRUE(feq(v[2], z));
}

void test_02_load_positions_and_indices(void) {
  const char* path = sample_path();

  gltf_error err = { 0 };

  int rc = gltf_load_file(path, &g_doc, &err);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(GLTF_OK, rc, err.message ? err.message : "load_file failed");
  TEST_ASSERT_NOT_NULL(g_doc);

  TEST_ASSERT_TRUE(gltf_doc_mesh_count(g_doc) > 0);

  const uint32_t mesh_index = 0;
  const uint32_t prim_i = 0;

  TEST_ASSERT_TRUE(gltf_doc_mesh_primitive_count(g_doc, mesh_index) > 0);

  // Test the mesh->primitive mapping
  uint32_t prim_index = 0;
  TEST_ASSERT_EQUAL_INT(
    1,
    gltf_doc_mesh_primitive(g_doc, mesh_index, prim_i, &prim_index)
  );
  TEST_ASSERT_EQUAL_INT(0, prim_index);

  // Resolve primitive
  uint32_t pos_acc = 0;
  int32_t  idx_acc = -1;

  TEST_ASSERT_EQUAL_INT(
    1,
    gltf_mesh_primitive_get_accessors(g_doc, mesh_index, prim_i, &pos_acc, &idx_acc)
  );
  TEST_ASSERT_TRUE(idx_acc >= 0);

  // Check accessor info: POSITION
  uint32_t count = 0, comp = 0, type = 0;
  int norm = 0;

  TEST_ASSERT_EQUAL_INT(1, gltf_doc_accessor_info(g_doc, pos_acc, &count, &comp, &type, &norm));
  TEST_ASSERT_EQUAL_UINT32(4, count);
  TEST_ASSERT_EQUAL_UINT32(GLTF_COMP_F32, comp);
  TEST_ASSERT_EQUAL_UINT32(GLTF_ACCESSOR_VEC3, type);
  TEST_ASSERT_EQUAL_INT(0, norm);

  // Check span: POSITION
  gltf_span sp = { 0 };
  rc = gltf_accessor_span(g_doc, pos_acc, &sp, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, rc, err.message ? err.message : "span failed");

  TEST_ASSERT_EQUAL_UINT32(4, sp.count);
  TEST_ASSERT_EQUAL_UINT32(12, sp.elem_size); // VEC3 f32
  TEST_ASSERT_EQUAL_UINT32(12, sp.stride);    // packed: 48 bytes / 4 verts

  // Read verts and calc AABB
  float mn[3] = {0};
  float mx[3] = {0};

  rc = gltf_compute_aabb_pos3_f32_span(g_doc, pos_acc, mn, mx, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, rc, err.message ? err.message : "compute_aabb failed");

  // accessor.min/max:
  assert_vec3_eq(mn, -1.f, 0.f, -1.f);
  assert_vec3_eq(mx,  1.f, 0.f,  1.f);

  // Check accessor info: indices
  TEST_ASSERT_EQUAL_INT(1, gltf_doc_accessor_info(g_doc, idx_acc, &count, &comp, &type, &norm));
  TEST_ASSERT_EQUAL_UINT32(6, count);
  TEST_ASSERT_EQUAL_UINT32(GLTF_COMP_U16, comp);
  TEST_ASSERT_EQUAL_UINT32(GLTF_ACCESSOR_SCALAR, type);

  // Check span: indices
  gltf_span isp = {0};
  rc = gltf_accessor_span(g_doc, idx_acc, &isp, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, rc, err.message ? err.message : "span indices failed");
  TEST_ASSERT_EQUAL_UINT32(6, isp.count);
  TEST_ASSERT_EQUAL_UINT32(2, isp.elem_size);
  TEST_ASSERT_EQUAL_UINT32(2, isp.stride);

  // Check indices (raw span, little-endian)
  uint16_t idx[6];
  for (uint32_t i = 0; i < 6; i++) {
    const uint8_t* p = isp.ptr + i * isp.stride;
    idx[i] = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    TEST_ASSERT_TRUE(idx[i] < 4);
  }

  // plane from Blender: 0, 1, 3, 0, 3, 2
  const uint16_t pat[6] = {0, 1, 3, 0, 3, 2};
  for (int i = 0; i < 6; i++) {
    TEST_ASSERT_EQUAL_UINT16(pat[i], idx[i]);
  }
}

void test_02_embedded_load_positions(void) {
  const char* path = sample_path_embedded();

  gltf_error err = {0};

  int rc = gltf_load_file(path, &g_doc, &err);

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(GLTF_OK, rc, err.message ? err.message : "load_file failed");
  TEST_ASSERT_NOT_NULL(g_doc);

  TEST_ASSERT_TRUE(gltf_doc_mesh_count(g_doc) > 0);

  const uint32_t mesh_index = 0;
  const uint32_t prim_i = 0;

  // Resolve primitive
  uint32_t pos_acc = 0;
  int32_t  idx_acc = -1;

  TEST_ASSERT_EQUAL_INT(
    1,
    gltf_mesh_primitive_get_accessors(g_doc, mesh_index, prim_i, &pos_acc, &idx_acc)
  );
  TEST_ASSERT_TRUE(idx_acc >= 0);

  // Check span: POSITION
  gltf_span sp = {0};
  rc = gltf_accessor_span(g_doc, pos_acc, &sp, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, rc, err.message ? err.message : "span failed");

  TEST_ASSERT_EQUAL_UINT32(4, sp.count);
  TEST_ASSERT_EQUAL_UINT32(12, sp.elem_size); // VEC3 f32
  TEST_ASSERT_EQUAL_UINT32(12, sp.stride);    // packed: 48 bytes / 4 verts
  
  // Read verts and calc AABB
  float mn[3] = {0};
  float mx[3] = {0};

  rc = gltf_compute_aabb_pos3_f32_span(g_doc, pos_acc, mn, mx, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, rc, err.message ? err.message : "compute_aabb failed");

  // accessor.min/max:
  assert_vec3_eq(mn, -1.f, 0.f, -1.f);
  assert_vec3_eq(mx,  1.f, 0.f,  1.f);
}
