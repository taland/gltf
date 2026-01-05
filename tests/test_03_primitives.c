#include "unity.h"

#include "gltf/gltf.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/03-tri.gltf";
}


void test_03_primitives(void) {
  const char* path = sample_path();

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mesh_count = gltf_doc_mesh_count(doc);
  TEST_ASSERT_TRUE_MESSAGE(mesh_count > 0, "expected at least 1 mesh");

  for (uint32_t mi = 0; mi < mesh_count; mi++) {
    uint32_t prim_count = gltf_doc_mesh_primitive_count(doc, mi);
    TEST_ASSERT_TRUE_MESSAGE(prim_count > 0, "mesh has no primitives");

    for (uint32_t pi = 0; pi < prim_count; pi++) {
      uint32_t prim_index = 0;
      TEST_ASSERT_TRUE_MESSAGE(
        gltf_doc_mesh_primitive(doc, mi, pi, &prim_index) == 1,
        "gltf_doc_mesh_primitive failed");

      // mode
      gltf_prim_mode mode = gltf_doc_primitive_mode(doc, prim_index);
      TEST_ASSERT_TRUE_MESSAGE((uint32_t)mode <= 6, "primitive mode out of range");

      // attributes present
      uint32_t attr_count = gltf_doc_primitive_attribute_count(doc, prim_index);
      TEST_ASSERT_TRUE_MESSAGE(attr_count > 0, "primitive has no attributes");

      // POSITION must exist
      uint32_t pos_accessor = 0;
      TEST_ASSERT_TRUE_MESSAGE(
        gltf_doc_primitive_find_attribute(
          doc,
          prim_index,
          GLTF_ATTR_POSITION, 0,
          &pos_accessor
        ) == 1,
        "primitive missing POSITION");

      // POSITION must be VEC3
      uint32_t a_count = 0, a_comp = 0, a_type = 0;
      int a_norm = 0;
      TEST_ASSERT_TRUE_MESSAGE(
        gltf_doc_accessor_info(
          doc, pos_accessor,
          &a_count, &a_comp, &a_type, &a_norm
        ) == 1,
        "accessor_info(POSITION) failed");
      TEST_ASSERT_TRUE_MESSAGE(a_type == GLTF_ACCESSOR_VEC3, "POSITION accessor not VEC3");

      // indices (optional)
      int32_t idx_accessor = -1;
      int has_idx = gltf_doc_primitive_indices_accessor(doc, prim_index, &idx_accessor);

      if (has_idx) {
        TEST_ASSERT_TRUE_MESSAGE(idx_accessor >= 0, "indices accessor negative but has_idx=1");

        uint32_t i_count = 0, i_comp = 0, i_type = 0;
        int i_norm = 0;
        TEST_ASSERT_TRUE_MESSAGE(gltf_doc_accessor_info(doc, (uint32_t)idx_accessor,
                                     &i_count, &i_comp, &i_type, &i_norm) == 1,
               "accessor_info(indices) failed");
        TEST_ASSERT_TRUE_MESSAGE(i_type == GLTF_ACCESSOR_SCALAR, "indices accessor not SCALAR");
        TEST_ASSERT_TRUE_MESSAGE(i_comp == GLTF_COMP_U8 || i_comp == GLTF_COMP_U16 || i_comp == GLTF_COMP_U32,
               "indices componentType not U8/U16/U32");

        // compute min/max index by reading indices
        uint32_t min_i = 0xFFFFFFFFu;
        uint32_t max_i = 0;

        for (uint32_t ii = 0; ii < i_count; ii++) {
          uint32_t v = 0;
          gltf_result rr = gltf_mesh_primitive_read_index_u32(doc, mi, pi, ii, &v, &err);
          TEST_ASSERT_TRUE_MESSAGE(rr == GLTF_OK, "read_index_u32 failed");
          if (v < min_i) min_i = v;
          if (v > max_i) max_i = v;
        }

        TEST_ASSERT_TRUE_MESSAGE(min_i <= max_i, "min/max index invalid");
        TEST_ASSERT_TRUE_MESSAGE(max_i < a_count, "max index >= vertex_count (POSITION.count)");

        if (mode == GLTF_PRIM_TRIANGLES) {
          TEST_ASSERT_TRUE_MESSAGE((i_count % 3) == 0, "TRIANGLES index_count not divisible by 3");
        }
      }
    }
  }
}


typedef struct tri_stats {
  uint32_t tri_calls;
  uint32_t min_i;
  uint32_t max_i;
  uint32_t stop_after; // 0 = no early stop
} tri_stats;

static gltf_iter_result on_tri(const gltf_tri* tri, uint32_t tri_index, void* user) {
  (void)tri_index;
  tri_stats* s = (tri_stats*)user;

  s->tri_calls++;

  if (tri->i0 < s->min_i) s->min_i = tri->i0;
  if (tri->i1 < s->min_i) s->min_i = tri->i1;
  if (tri->i2 < s->min_i) s->min_i = tri->i2;

  if (tri->i0 > s->max_i) s->max_i = tri->i0;
  if (tri->i1 > s->max_i) s->max_i = tri->i1;
  if (tri->i2 > s->max_i) s->max_i = tri->i2;

  if (s->stop_after != 0 && s->tri_calls >= s->stop_after) {
    return GLTF_ITER_STOP;
  }
  return GLTF_ITER_CONTINUE;
}

static uint32_t expected_tri_count(gltf_prim_mode mode, uint32_t n) {
  switch (mode) {
    case GLTF_PRIM_TRIANGLES:
      return (n / 3);
    case GLTF_PRIM_TRIANGLE_STRIP:
    case GLTF_PRIM_TRIANGLE_FAN:
      return (n >= 3) ? (n - 2) : 0;
    default:
      return 0;
  }
}


void test_03_iterate_triangles(void) {
  const char* path = sample_path();

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mesh_count = gltf_doc_mesh_count(doc);
  TEST_ASSERT_TRUE_MESSAGE(mesh_count > 0, "expected at least 1 mesh");

  uint32_t mi = 0;
  uint32_t prim_count = gltf_doc_mesh_primitive_count(doc, mi);

  uint32_t pi = 0;
  uint32_t prim_index = 0;
  TEST_ASSERT_TRUE_MESSAGE(
    gltf_doc_mesh_primitive(doc, mi, pi, &prim_index) == 1,
    "gltf_doc_mesh_primitive failed");

  // Get vertex_count via POSITION accessor
  uint32_t pos_accessor = 0;
  TEST_ASSERT_TRUE_MESSAGE(
    gltf_doc_primitive_find_attribute(doc, prim_index,
                                      GLTF_ATTR_POSITION, 0,
                                      &pos_accessor) == 1,
    "primitive missing POSITION");

  uint32_t v_count = 0, v_comp = 0, v_type = 0;
  int v_norm = 0;
  TEST_ASSERT_TRUE_MESSAGE(
    gltf_doc_accessor_info(doc, pos_accessor,
                           &v_count, &v_comp, &v_type, &v_norm) == 1,
    "accessor_info(POSITION) failed");
  TEST_ASSERT_TRUE_MESSAGE(v_count > 0, "POSITION.count == 0");
  TEST_ASSERT_TRUE_MESSAGE(v_type == GLTF_ACCESSOR_VEC3, "POSITION accessor not VEC3");
  TEST_ASSERT_TRUE_MESSAGE(v_comp == GLTF_COMP_F32, "POSITION componentType not F32");
  TEST_ASSERT_TRUE_MESSAGE(v_norm == 0, "POSITION normalized != false");

  // Indices (optional)
  int32_t idx_accessor = -1;
  int has_idx = gltf_doc_primitive_indices_accessor(doc, prim_index, &idx_accessor);
  TEST_ASSERT_TRUE_MESSAGE(has_idx == 1, "expected primitive to have indices");
  TEST_ASSERT_TRUE_MESSAGE(idx_accessor >= 0, "indices accessor negative but has_idx=1");

  gltf_prim_mode mode = gltf_doc_primitive_mode(doc, prim_index);
  TEST_ASSERT_TRUE_MESSAGE(mode == GLTF_PRIM_TRIANGLES || mode == GLTF_PRIM_TRIANGLE_STRIP || mode == GLTF_PRIM_TRIANGLE_FAN, "unsupported mode");

  uint32_t i_count = 0, i_comp = 0, i_type = 0;
  int i_norm = 0;
  TEST_ASSERT_TRUE_MESSAGE(
    gltf_doc_accessor_info(doc, (uint32_t)idx_accessor,
                           &i_count, &i_comp, &i_type, &i_norm) == 1,
    "accessor_info(indices) failed");
  TEST_ASSERT_TRUE_MESSAGE(i_count > 0, "indices.count == 0");
  if (mode == GLTF_PRIM_TRIANGLES) {
    TEST_ASSERT_TRUE_MESSAGE(i_count % 3 == 0, "expected TRIANGLES with index_count not divisible by 3");
  }
  TEST_ASSERT_TRUE_MESSAGE(
    i_comp == GLTF_COMP_U8 || i_comp == GLTF_COMP_U16 || i_comp == GLTF_COMP_U32,
    "indices componentType not U8/U16/U32");
  TEST_ASSERT_TRUE_MESSAGE(i_type == GLTF_ACCESSOR_SCALAR, "indices accessor not SCALAR");

  uint32_t n = v_count;
  uint32_t min_idx = 0;
  uint32_t max_idx = v_count - 1;

  n = i_count;

  // Compute expected min/max by reading raw indices
  min_idx = 0xFFFFFFFFu;
  max_idx = 0;
  for (uint32_t ii = 0; ii < i_count; ii++) {
    uint32_t v = 0;
    gltf_result rr = gltf_mesh_primitive_read_index_u32(doc, mi, pi, ii, &v, &err);
    TEST_ASSERT_TRUE_MESSAGE(rr == GLTF_OK, "read_index_u32 failed");
    if (v < min_idx) min_idx = v;
    if (v > max_idx) max_idx = v;
  }
  TEST_ASSERT_TRUE_MESSAGE(max_idx < v_count, "index max >= vertex_count");

  tri_stats s;
  memset(&s, 0, sizeof(s));
  s.min_i = 0xFFFFFFFFu;
  s.max_i = 0;

  gltf_result it_r = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &s, &err);
  TEST_ASSERT_TRUE_MESSAGE(it_r == GLTF_OK, "iterate_triangles must return GLTF_OK for supported modes");

  // Expected triangle count
  uint32_t exp_tris = expected_tri_count(mode, n);

  // If you choose to treat TRIANGLES with N%3!=0 as parse error, enforce it here:
  if (mode == GLTF_PRIM_TRIANGLES) {
    TEST_ASSERT_TRUE_MESSAGE((n % 3) == 0, "TRIANGLES: index/vertex count not divisible by 3 (test fixture)");
  }

  TEST_ASSERT_TRUE_MESSAGE(s.tri_calls == exp_tris, "triangle callback call count mismatch");

  // Min/max index must match expected range
  if (exp_tris > 0) {
    TEST_ASSERT_TRUE_MESSAGE(s.min_i == min_idx, "triangle min index mismatch");
    TEST_ASSERT_TRUE_MESSAGE(s.max_i == max_idx, "triangle max index mismatch");
  }

  // All indices are in range (implicit via min/max check) — but add direct sanity:
  TEST_ASSERT_TRUE_MESSAGE(s.max_i < v_count || exp_tris == 0, "triangle index out of range");

  // Early stop test: stop after 1 triangle if there is at least 1
  if (exp_tris > 0) {
    tri_stats s2;
    memset(&s2, 0, sizeof(s2));
    s2.min_i = 0xFFFFFFFFu;
    s2.max_i = 0;
    s2.stop_after = 1;

    gltf_result it2 = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &s2, &err);

    TEST_ASSERT_TRUE_MESSAGE(it2 == GLTF_OK, "early-stop iterate must return GLTF_OK");
    TEST_ASSERT_TRUE_MESSAGE(s2.tri_calls == 1, "early-stop must stop after 1 triangle");
  }
}


typedef struct tri_expect {
  uint32_t calls;
  gltf_tri expected[8];
  uint32_t exp_count;
  uint32_t last_tri_index;
} tri_expect;

static gltf_iter_result on_tri_expect(const gltf_tri* tri, uint32_t tri_index, void* user) {
  tri_expect* e = (tri_expect*)user;

  TEST_ASSERT_TRUE_MESSAGE(e->calls < e->exp_count, "triangle callback called too many times");
  const gltf_tri* ex = &e->expected[e->calls];

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ex->i0, tri->i0, "tri.i0 mismatch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ex->i1, tri->i1, "tri.i1 mismatch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(ex->i2, tri->i2, "tri.i2 mismatch");

  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e->calls, tri_index, "tri_index must be sequential 0..");
  e->last_tri_index = tri_index;

  e->calls++;
  return GLTF_ITER_CONTINUE;
}

static const char* tri_strip_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/03-tri_strip.gltf";
}

static const char* tri_strip_noidx_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/03-tri_strip_noidx.gltf";
}


void test_03_iterate_triangles_triangle_strip_indexed(void) {
  const char* path = tri_strip_path();

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mi = 0;
  uint32_t pi = 0;
  uint32_t prim_index = 0;

  tri_expect e;
  memset(&e, 0, sizeof(e));
  e.exp_count = 2;

  // STRIP with 4 vertices => 2 triangles:
  // t=0 even: (0,1,2)
  // t=1 odd:  (1,0,3)  (winding flip)
  e.expected[0] = (gltf_tri){0, 1, 2};
  e.expected[1] = (gltf_tri){1, 0, 3}; // odd flip

  gltf_result it_r = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &e, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, r, "iterate_triangles failed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.exp_count, e.calls, "strip triangle count mismatch");
}

void test_03_iterate_triangles_triangle_strip_non_indexed(void) {
  const char* path = tri_strip_noidx_path();

  gltf_doc* doc = NULL;
  gltf_error err = (gltf_error){0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mi = 0;
  uint32_t pi = 0;
  uint32_t prim_index = 0;

  tri_expect e;
  memset(&e, 0, sizeof(e));
  e.exp_count = 2;

  // STRIP with 4 vertices => 2 triangles:
  // t=0 even: (0,1,2)
  // t=1 odd:  (1,0,3)  (winding flip)
  e.expected[0] = (gltf_tri){0, 1, 2};
  e.expected[1] = (gltf_tri){1, 0, 3};

  gltf_result it_r = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &e, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, it_r, "iterate_triangles failed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.exp_count, e.calls, "strip triangle count mismatch");

  gltf_free(doc);
}


static const char* tri_fan_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/03-tri_fan.gltf";
}

static const char* tri_fan_noidx_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/03-tri_fan_noidx.gltf";
}


void test_03_iterate_triangles_triangle_fan_indexed(void) {
  const char* path = tri_fan_path();

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mi = 0;
  uint32_t pi = 0;
  uint32_t prim_index = 0;

  tri_expect e;
  memset(&e, 0, sizeof(e));
  e.exp_count = 2;

  // FAN with 4 vertices => 2 triangles:
  // (0,1,2), (0,2,3)
  e.expected[0] = (gltf_tri){0, 1, 2};
  e.expected[1] = (gltf_tri){0, 2, 3};

  gltf_result it_r = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &e, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, r, "iterate_triangles failed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.exp_count, e.calls, "fan triangle count mismatch");
}

void test_03_iterate_triangles_triangle_fan_non_indexed(void) {
  const char* path = tri_fan_noidx_path();

  gltf_doc* doc = NULL;
  gltf_error err = (gltf_error){0};

  gltf_result r = gltf_load_file(path, &doc, &err);
  TEST_ASSERT_TRUE_MESSAGE(r == GLTF_OK && doc != NULL, "gltf_load_file failed");

  uint32_t mi = 0;
  uint32_t pi = 0;
  uint32_t prim_index = 0;

  tri_expect e;
  memset(&e, 0, sizeof(e));
  e.exp_count = 2;

  // FAN with 4 vertices => 2 triangles:
  // (0,1,2), (0,2,3)
  e.expected[0] = (gltf_tri){0, 1, 2};
  e.expected[1] = (gltf_tri){0, 2, 3};

  gltf_result it_r = gltf_doc_primitive_iterate_triangles(doc, prim_index, on_tri, &e, &err);
  TEST_ASSERT_EQUAL_INT_MESSAGE(GLTF_OK, it_r, "iterate_triangles failed");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(e.exp_count, e.calls, "fan triangle count mismatch");

  gltf_free(doc);
}
