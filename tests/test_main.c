#include "unity.h"

#include "gltf/gltf.h"

gltf_doc* g_doc = NULL;

// Test declarations
void test_load_sample_minimal(void);

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

  RUN_TEST(test_load_sample_minimal);

  return UNITY_END();
}
