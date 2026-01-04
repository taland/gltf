#include "unity.h"

#include "gltf/gltf.h"

gltf_doc* g_doc = NULL;

// Test declarations
void test_01_load_sample_minimal(void);
void test_02_load_positions_and_indices(void);
void test_02_embedded_load_positions(void);
void test_03_primitives(void);
void test_03_iterate_triangles(void);

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

  return UNITY_END();
}
