#include "unity.h"

#include "gltf/gltf.h"

gltf_doc* g_doc = NULL;

// Test declarations
void test_01_load_sample_minimal(void);
void test_02_load_positions_and_indices(void);
void test_02_embedded_load_positions(void);
void test_03_primitives(void);
void test_03_iterate_triangles(void);
void test_03_iterate_triangles_triangle_strip_indexed(void);
void test_03_iterate_triangles_triangle_strip_non_indexed(void);
void test_03_iterate_triangles_triangle_fan_indexed(void);
void test_03_iterate_triangles_triangle_fan_non_indexed(void);
void test_04_world_matrices_trs_and_matrix(void);
void test_05_materials(void);
void test_06_images_datauri(void);

void setUp(void) {
    g_doc = NULL;
}
void tearDown(void) {
    if (g_doc) {
        gltf_free(g_doc);
        g_doc = NULL;
    }
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_01_load_sample_minimal);
  RUN_TEST(test_02_load_positions_and_indices);
  RUN_TEST(test_02_embedded_load_positions);
  RUN_TEST(test_03_primitives);
  RUN_TEST(test_03_iterate_triangles);
  RUN_TEST(test_03_iterate_triangles_triangle_strip_indexed);
  RUN_TEST(test_03_iterate_triangles_triangle_strip_non_indexed);
  RUN_TEST(test_03_iterate_triangles_triangle_fan_indexed);
  RUN_TEST(test_03_iterate_triangles_triangle_fan_non_indexed);
  RUN_TEST(test_04_world_matrices_trs_and_matrix);
  RUN_TEST(test_05_materials);
  RUN_TEST(test_06_images_datauri);

  return UNITY_END();
}
