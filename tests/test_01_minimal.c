#include "unity.h"

#include "gltf/gltf.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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

void test_01_load_gltf_from_memory(void) {
  /*
    BIN layout (42 bytes, padded to 44):
    - positions: 3 * vec3 f32 = 36 bytes
    - indices:   3 * u16     = 6 bytes
    - padding:   2 bytes

    base64 generated once and hardcoded
  */
    const char* json =
      "{"
      "  \"asset\": { \"version\": \"2.0\" },"
      "  \"buffers\": [ {"
      "    \"uri\": \"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=\","
      "    \"byteLength\": 44"
      "  } ],"
      "  \"bufferViews\": ["
      "    { \"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 36 },"
      "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 6 }"
      "  ],"
      "  \"accessors\": ["
      "    { \"bufferView\": 0, \"byteOffset\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },"
      "    { \"bufferView\": 1, \"byteOffset\": 0, \"componentType\": 5123, \"count\": 3, \"type\": \"SCALAR\" }"
      "  ],"
      "  \"meshes\": [ {"
      "    \"primitives\": [ { \"attributes\": { \"POSITION\": 0 }, \"indices\": 1 } ]"
      "  } ],"
      "  \"nodes\": ["
      "    { \"mesh\": 0 },"
      "    { \"name\": \"dummy\" }"
      "  ],"
      "  \"scenes\": [ { \"nodes\": [ 0 ] } ],"
      "  \"scene\": 0"
      "}";

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result rc =
    gltf_load_json_string((uint8_t*)json,
                          (uint32_t)strlen(json),
                          &doc,
                          &err);

  if (rc != GLTF_OK) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "gltf_load_json_string failed rc=%d msg=%s path=%s",
             rc,
             err.message ? err.message : "(null)",
             err.path ? err.path : "(null)");
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_NOT_NULL(doc);

  const char* ver = gltf_doc_asset_version(doc);
  TEST_ASSERT_NOT_NULL(ver);
  TEST_ASSERT_EQUAL_STRING("2.0", ver);

  TEST_ASSERT_EQUAL_UINT32(1, gltf_doc_mesh_count(doc));
  TEST_ASSERT_EQUAL_UINT32(1, gltf_doc_mesh_primitive_count(doc, 0));

  gltf_span pos = {0};

  rc = gltf_mesh_primitive_position_span(doc, 0, 0, &pos, &err);

  if (rc != GLTF_OK) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "position_span failed rc=%d msg=%s path=%s",
             rc,
             err.message ? err.message : "(null)",
             err.path ? err.path : "(null)");
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_EQUAL_UINT32(3, pos.count);
  TEST_ASSERT_EQUAL_UINT32(12, pos.elem_size);

  float v1[3];
  rc = gltf_mesh_primitive_read_position_f32(doc, 0, 0, 1, v1, &err);
  TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v1[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, v1[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, v1[2]);

  uint32_t icount = 0;
  rc = gltf_mesh_primitive_index_count(doc, 0, 0, &icount, &err);
  TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);
  TEST_ASSERT_EQUAL_UINT32(3, icount);

  uint32_t idx = 999;
  rc = gltf_mesh_primitive_read_index_u32(doc, 0, 0, 2, &idx, &err);
  TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);
  TEST_ASSERT_EQUAL_UINT32(2, idx);
  
  gltf_free(doc);
}