#include "unity.h"

#include "gltf/gltf.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_trs_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/04-world_trs.gltf";
}

// Add this second fixture to test explicit "matrix" behavior.
static const char* sample_matrix_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/04-world_matrix.gltf";
}

// Column-major: m[col*4 + row]
static void mat4_extract_translation(const float m[16], float out_t3[3]) {
  out_t3[0] = m[12];
  out_t3[1] = m[13];
  out_t3[2] = m[14];
}

// Scale = lengths of basis columns (robust for rotation + non-uniform scale)
static void mat4_extract_scale(const float m[16], float out_s3[3]) {
  const float x0 = m[0],  x1 = m[1],  x2 = m[2];
  const float y0 = m[4],  y1 = m[5],  y2 = m[6];
  const float z0 = m[8],  z1 = m[9],  z2 = m[10];

  out_s3[0] = sqrtf(x0*x0 + x1*x1 + x2*x2);
  out_s3[1] = sqrtf(y0*y0 + y1*y1 + y2*y2);
  out_s3[2] = sqrtf(z0*z0 + z1*z1 + z2*z2);
}

static void assert_vec3_close(const float a[3], const float b[3], float eps) {
  TEST_ASSERT_FLOAT_WITHIN(eps, b[0], a[0]);
  TEST_ASSERT_FLOAT_WITHIN(eps, b[1], a[1]);
  TEST_ASSERT_FLOAT_WITHIN(eps, b[2], a[2]);
}

static void assert_mat4_close(const float a[16], const float b[16], float eps) {
  for (int i = 0; i < 16; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(eps, b[i], a[i]);
  }
}

static void load_or_fail(const char* path) {
  gltf_error err = {0};
  int rc = gltf_load_file(path, &g_doc, &err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf, sizeof(buf),
                   "gltf_load_file('%s') failed rc=%d msg=%s path=%s line=%d col=%d",
                   path, rc,
                   err.message ? err.message : "(null)",
                   err.path ? err.path : "(null)",
                   err.line, err.col);
    TEST_FAIL_MESSAGE(buf);
  }
  TEST_ASSERT_NOT_NULL(g_doc);
}

void test_04_world_matrices_trs_and_matrix(void) {
  const float eps = 1e-5f;

  // --------------------------------------------------------------------------
  // Part A: TRS fixture (04-world_trs.gltf) : 3 nodes RootA->ChildB->ChildC
  // --------------------------------------------------------------------------
  {
    load_or_fail(sample_trs_path());

    TEST_ASSERT_EQUAL_UINT(1u, gltf_doc_scene_count(g_doc));
    TEST_ASSERT_EQUAL_UINT(3u, gltf_doc_node_count(g_doc));
    TEST_ASSERT_EQUAL_UINT(3u, gltf_doc_mesh_count(g_doc));

    // Local matrices must reflect TRS:
    // node[2] RootA: T=(1,0,0), S=(2,2,2), R=identity
    // node[1] ChildB: T=(0,0,-3), S=(1,1,1), R=identity
    // node[0] ChildC: T=(0,4,0), S=(1,1,1), R=non-identity (quat given)

    float L2[16], L1[16], L0[16];
    TEST_ASSERT_EQUAL_INT(1, gltf_node_local_matrix(g_doc, 2u, L2));
    TEST_ASSERT_EQUAL_INT(1, gltf_node_local_matrix(g_doc, 1u, L1));
    TEST_ASSERT_EQUAL_INT(1, gltf_node_local_matrix(g_doc, 0u, L0));

    float t[3], s[3];

    mat4_extract_translation(L2, t);
    mat4_extract_scale(L2, s);
    assert_vec3_close(t, (float[3]){1.f, 0.f, 0.f}, eps);
    assert_vec3_close(s, (float[3]){2.f, 2.f, 2.f}, eps);

    mat4_extract_translation(L1, t);
    mat4_extract_scale(L1, s);
    assert_vec3_close(t, (float[3]){0.f, 0.f, -3.f}, eps);
    assert_vec3_close(s, (float[3]){1.f, 1.f, 1.f}, eps);

    mat4_extract_translation(L0, t);
    mat4_extract_scale(L0, s);
    assert_vec3_close(t, (float[3]){0.f, 4.f, 0.f}, eps);
    assert_vec3_close(s, (float[3]){1.f, 1.f, 1.f}, eps);

    // Rotation should be non-identity for ChildC.
    // Quick check: 3x3 part shouldn't match identity exactly.
    // (This is intentionally loose: we only want to know rotation was applied.)
    TEST_ASSERT_FALSE(fabsf(L0[0] - 1.f) < 1e-6f &&
                      fabsf(L0[5] - 1.f) < 1e-6f &&
                      fabsf(L0[10] - 1.f) < 1e-6f);

    // Compute world matrices
    gltf_world_cache* cache = NULL;
    gltf_error err = {0};
    int rc = gltf_world_cache_create(g_doc, &cache, &err);
    TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);
    TEST_ASSERT_NOT_NULL(cache);

    uint32_t scene_index =
      (gltf_doc_default_scene(g_doc) >= 0) ? (uint32_t)gltf_doc_default_scene(g_doc) : 0u;

    rc = gltf_compute_world_matrices(g_doc, scene_index, cache, &err);
    if (rc != GLTF_OK) {
      char buf[512];
      (void)snprintf(buf, sizeof(buf),
                     "gltf_compute_world_matrices failed rc=%d msg=%s path=%s line=%d col=%d",
                     rc,
                     err.message ? err.message : "(null)",
                     err.path ? err.path : "(null)",
                     err.line, err.col);
      TEST_FAIL_MESSAGE(buf);
    }

    // Expected world translations from your example:
    // RootA:  (1,0,0), S=(2,2,2)
    // ChildB: (1,0,-6), S=(2,2,2)  because parent scale 2 affects child local T z=-3 -> -6
    // ChildC: (1,8,-6), S=(2,2,2)  because parent scale 2 affects child local T y=4 -> 8
    float W2[16], W1[16], W0[16];
    TEST_ASSERT_EQUAL_INT(1, gltf_world_matrix(g_doc, cache, 2u, W2));
    TEST_ASSERT_EQUAL_INT(1, gltf_world_matrix(g_doc, cache, 1u, W1));
    TEST_ASSERT_EQUAL_INT(1, gltf_world_matrix(g_doc, cache, 0u, W0));

    mat4_extract_translation(W2, t);
    mat4_extract_scale(W2, s);
    assert_vec3_close(t, (float[3]){1.f, 0.f, 0.f}, eps);
    assert_vec3_close(s, (float[3]){2.f, 2.f, 2.f}, eps);

    mat4_extract_translation(W1, t);
    mat4_extract_scale(W1, s);
    assert_vec3_close(t, (float[3]){1.f, 0.f, -6.f}, eps);
    assert_vec3_close(s, (float[3]){2.f, 2.f, 2.f}, eps);

    mat4_extract_translation(W0, t);
    mat4_extract_scale(W0, s);
    assert_vec3_close(t, (float[3]){1.f, 8.f, -6.f}, eps);
    assert_vec3_close(s, (float[3]){2.f, 2.f, 2.f}, eps);

    gltf_world_cache_free(cache);
    gltf_free(g_doc);
    g_doc = NULL;
  }

  // --------------------------------------------------------------------------
  // Part B: Matrix fixture (04-world_matrix.gltf): prove "matrix" is taken from file
  // --------------------------------------------------------------------------
  {
    load_or_fail(sample_matrix_path());

    TEST_ASSERT_EQUAL_UINT(1u, gltf_doc_scene_count(g_doc));
    TEST_ASSERT_EQUAL_UINT(1u, gltf_doc_node_count(g_doc));

    // This fixture should contain node[0] with BOTH:
    //   - "matrix": explicit non-identity
    //   - AND also some TRS fields that are different (to prove TRS is ignored).
    //
    // We can't access has_matrix via public API, so we infer behavior by checking local matrix.

    float L[16];
    TEST_ASSERT_EQUAL_INT(1, gltf_node_local_matrix(g_doc, 0u, L));

    const float expected[16] = {
      2,0,0,0,
      0,3,0,0,
      0,0,4,0,
      5,6,7,1
    }; // column-major: scale (2,3,4) + translation (5,6,7)

    assert_mat4_close(L, expected, eps);

    gltf_free(g_doc);
    g_doc = NULL;
  }
}
