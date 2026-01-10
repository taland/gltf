#include "unity.h"

#include "gltf/gltf.h"

#include <stdio.h>

#ifndef GLTF_REPO_ROOT
#error "GLTF_REPO_ROOT must be defined by the build system"
#endif

extern gltf_doc* g_doc;

static const char* sample_path(void) {
  return GLTF_REPO_ROOT "/tests/fixtures/01-minimal.gltf";
}

void test_01_load_sample_minimal(void) {
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
  TEST_ASSERT_EQUAL_STRING("2.0", gltf_doc_asset_version(g_doc));
  TEST_ASSERT_EQUAL_UINT(1u, gltf_doc_scene_count(g_doc));
  TEST_ASSERT_EQUAL_UINT(2u, gltf_doc_node_count(g_doc));
  TEST_ASSERT_EQUAL_UINT(1u, gltf_doc_mesh_count(g_doc));
  TEST_ASSERT_EQUAL_INT32(0, gltf_doc_default_scene(g_doc));

  //
  // scene API
  TEST_ASSERT_NULL(gltf_doc_scene_name(g_doc, 0));

  // scene->nodes API
  TEST_ASSERT_EQUAL_UINT32(1u, gltf_doc_scene_node_count(g_doc, 0));

  // Out-of-range scene count query
  TEST_ASSERT_EQUAL_UINT32(0u, gltf_doc_scene_node_count(g_doc, 1));

  {
    uint32_t node_idx;

    // Valid fetch
    TEST_ASSERT_EQUAL_INT(1, gltf_doc_scene_node(g_doc, 0, 0, &node_idx));
    TEST_ASSERT_EQUAL_UINT32(0u, node_idx);

    // Out-of-range scene index
    node_idx = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL_INT(0, gltf_doc_scene_node(g_doc, 1, 0, &node_idx));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, node_idx);

    // Out-of-range element index
    node_idx = 0xFFFFFFFFu;
    TEST_ASSERT_EQUAL_INT(0, gltf_doc_scene_node(g_doc, 0, 1, &node_idx));
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, node_idx);

    // NULL out pointer
    TEST_ASSERT_EQUAL_INT(0, gltf_doc_scene_node(g_doc, 0, 0, NULL));
  }

  //
  // node API
  TEST_ASSERT_NULL(gltf_doc_node_name(g_doc, 0));
  TEST_ASSERT_EQUAL_INT32(0, gltf_doc_node_mesh(g_doc, 0));
  TEST_ASSERT_EQUAL_UINT32(0u, gltf_doc_node_child_count(g_doc, 0));

  {
    uint32_t child = 123u;
    TEST_ASSERT_EQUAL_INT(0, gltf_doc_node_child(g_doc, 0, 0, &child)); // no children
    TEST_ASSERT_EQUAL_UINT32(123u, child);

    TEST_ASSERT_EQUAL_INT(0, gltf_doc_node_child(g_doc, 0, 0, NULL));
  }

  // node[1] = { "name": "dummy" }
  TEST_ASSERT_EQUAL_STRING("dummy", gltf_doc_node_name(g_doc, 1));
  TEST_ASSERT_EQUAL_INT32(-1, gltf_doc_node_mesh(g_doc, 1));
  TEST_ASSERT_EQUAL_UINT32(0u, gltf_doc_node_child_count(g_doc, 1));

  // Out-of-range node index behavior
  TEST_ASSERT_NULL(gltf_doc_node_name(g_doc, 2));
  TEST_ASSERT_EQUAL_INT32(-1, gltf_doc_node_mesh(g_doc, 2));
  TEST_ASSERT_EQUAL_UINT32(0u, gltf_doc_node_child_count(g_doc, 2));

  {
    uint32_t child = 777u;
    TEST_ASSERT_EQUAL_INT(0, gltf_doc_node_child(g_doc, 2, 0, &child));
    TEST_ASSERT_EQUAL_UINT32(777u, child);
  }

  //
  // mesh API
  TEST_ASSERT_NULL(gltf_doc_mesh_name(g_doc, 0)); // meshes[0] has no name in sample
  TEST_ASSERT_NULL(gltf_doc_mesh_name(g_doc, 1)); // out-of-range => NULL
}
