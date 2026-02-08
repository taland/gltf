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
  return GLTF_REPO_ROOT "/tests/fixtures/07-basic.glb";
}

void test_07_load_glb(void) {
  const char* path = sample_path();

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  gltf_result rc = gltf_load_file(path, &doc, &err);
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

  TEST_ASSERT_NOT_NULL(doc);

  // Minimal sanity checks on the document
  TEST_ASSERT_EQUAL_STRING_MESSAGE("2.0", gltf_doc_asset_version(doc),
                                   "asset.version must be '2.0' for glTF 2.0");

  uint32_t scene_count = gltf_doc_scene_count(doc);
  uint32_t node_count  = gltf_doc_node_count(doc);
  uint32_t mesh_count  = gltf_doc_mesh_count(doc);

  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, scene_count, "scene_count must be > 0");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, node_count,  "node_count must be > 0");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, mesh_count,  "mesh_count must be > 0");

  // Take the first mesh and the first primitive
  uint32_t prim_count = gltf_doc_mesh_primitive_count(doc, 0);
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, prim_count, "mesh[0] must have at least one primitive");

  // Verify POSITION is accessible (indirectly proves the BIN chunk is attached)
  gltf_span pos = {0};
  gltf_error span_err = {0};
  rc = gltf_mesh_primitive_position_span(doc, 0, 0, &pos, &span_err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_mesh_primitive_position_span(mesh=0 prim=0) failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   span_err.message ? span_err.message : "(null)",
                   span_err.path ? span_err.path : "(null)",
                   span_err.line,
                   span_err.col);
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_NOT_NULL_MESSAGE(pos.ptr, "POSITION span ptr must not be NULL");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, pos.count, "POSITION span count must be > 0");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, pos.stride, "POSITION span stride must be > 0");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(12, pos.elem_size, "POSITION elem_size must be 12 bytes (VEC3 f32)");

  // Read one position via the public decoder API
  float p0[3] = {0, 0, 0};
  gltf_error read_err = {0};
  rc = gltf_mesh_primitive_read_position_f32(doc, 0, 0, 0, p0, &read_err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_mesh_primitive_read_position_f32(mesh=0 prim=0 v=0) failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   read_err.message ? read_err.message : "(null)",
                   read_err.path ? read_err.path : "(null)",
                   read_err.line,
                   read_err.col);
    TEST_FAIL_MESSAGE(buf);
  }

  // Indices (if present)
  uint32_t index_count = 0;
  gltf_error ic_err = {0};
  rc = gltf_mesh_primitive_index_count(doc, 0, 0, &index_count, &ic_err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_mesh_primitive_index_count(mesh=0 prim=0) failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   ic_err.message ? ic_err.message : "(null)",
                   ic_err.path ? ic_err.path : "(null)",
                   ic_err.line,
                   ic_err.col);
    TEST_FAIL_MESSAGE(buf);
  }
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, index_count, "index_count must be > 0");

  // Check reading one index (should work for indexed and non-indexed cases)
  uint32_t idx0 = 0xFFFFFFFFu;
  gltf_error ri_err = {0};
  rc = gltf_mesh_primitive_read_index_u32(doc, 0, 0, 0, &idx0, &ri_err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_mesh_primitive_read_index_u32(mesh=0 prim=0 i=0) failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   ri_err.message ? ri_err.message : "(null)",
                   ri_err.path ? ri_err.path : "(null)",
                   ri_err.line,
                   ri_err.col);
    TEST_FAIL_MESSAGE(buf);
  }
  TEST_ASSERT_TRUE_MESSAGE(idx0 != 0xFFFFFFFFu, "read index must write out_index");

  // Finally, build the draw view (positions + index metadata)
  gltf_draw_primitive_view view;
  gltf_error v_err = {0};
  rc = gltf_mesh_primitive_view(doc, 0, 0, &view, &v_err);
  if (rc != GLTF_OK) {
    char buf[512];
    (void)snprintf(buf,
                   sizeof(buf),
                   "gltf_mesh_primitive_view(mesh=0 prim=0) failed rc=%d msg=%s path=%s line=%d col=%d",
                   rc,
                   v_err.message ? v_err.message : "(null)",
                   v_err.path ? v_err.path : "(null)",
                   v_err.line,
                   v_err.col);
    TEST_FAIL_MESSAGE(buf);
  }
  TEST_ASSERT_NOT_NULL_MESSAGE(view.positions.ptr, "view.positions must be valid");
  TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(0, view.index_count, "view.index_count must be > 0");

  gltf_free(doc);
}

static void write_u32_le(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8u) & 0xFFu);
  p[2] = (uint8_t)((v >> 16u) & 0xFFu);
  p[3] = (uint8_t)((v >> 24u) & 0xFFu);
}

static uint32_t pad4_u32(uint32_t n) {
  return (n + 3u) & ~3u;
}

// BIN: positions (3 * vec3 f32 = 36 bytes) + indices (3 * u16 = 6 bytes) + pad(2) = 44 bytes
static void build_minimal_bin(uint8_t out_bin[44]) {
  memset(out_bin, 0, 44);

  float pos[9] = {
    0.0f, 0.0f, 0.0f,  // v0
    1.0f, 0.0f, 0.0f,  // v1
    0.0f, 1.0f, 0.0f   // v2
  };
  memcpy(out_bin + 0, pos, 36);

  uint16_t idx[3] = {0, 1, 2};
  memcpy(out_bin + 36, idx, 6);
}

static void assert_minimal_triangle_loaded(const gltf_doc* doc) {
  TEST_ASSERT_NOT_NULL(doc);

  const char* ver = gltf_doc_asset_version(doc);
  TEST_ASSERT_NOT_NULL(ver);
  TEST_ASSERT_EQUAL_STRING("2.0", ver);

  TEST_ASSERT_GREATER_THAN_UINT32(0, gltf_doc_mesh_count(doc));
  TEST_ASSERT_GREATER_THAN_UINT32(0, gltf_doc_mesh_primitive_count(doc, 0));

  gltf_span pos = {0};
  gltf_error e = {0};
  gltf_result rc = gltf_mesh_primitive_position_span(doc, 0, 0, &pos, &e);
  if (rc != GLTF_OK) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "position_span failed rc=%d msg=%s path=%s line=%d col=%d",
             rc,
             e.message ? e.message : "(null)",
             e.path ? e.path : "(null)",
             e.line, e.col);
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_NOT_NULL(pos.ptr);
  TEST_ASSERT_EQUAL_UINT32(3u, pos.count);
  TEST_ASSERT_EQUAL_UINT32(12u, pos.elem_size);

  float v1[3] = {0};
  gltf_error e2 = {0};
  rc = gltf_mesh_primitive_read_position_f32(doc, 0, 0, 1, v1, &e2);
  if (rc != GLTF_OK) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "read_position_f32 failed rc=%d msg=%s path=%s line=%d col=%d",
             rc,
             e2.message ? e2.message : "(null)",
             e2.path ? e2.path : "(null)",
             e2.line, e2.col);
    TEST_FAIL_MESSAGE(buf);
  }

  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, v1[0]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, v1[1]);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, v1[2]);

  uint32_t ic = 0;
  gltf_error e3 = {0};
  rc = gltf_mesh_primitive_index_count(doc, 0, 0, &ic, &e3);
  TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);
  TEST_ASSERT_EQUAL_UINT32(3u, ic);

  uint32_t i0 = 999;
  gltf_error e4 = {0};
  rc = gltf_mesh_primitive_read_index_u32(doc, 0, 0, 0, &i0, &e4);
  TEST_ASSERT_EQUAL_INT(GLTF_OK, rc);
  TEST_ASSERT_EQUAL_UINT32(0u, i0);
}

void test_07_load_glb_from_mem(void) {
  // Minimal GLB JSON:
  // - buffers[0] has NO uri (GLB path)
  // - bufferViews: positions @0 len36, indices @36 len6
  // - accessors: positions (VEC3/F32), indices (SCALAR/U16)
  const char* json =
    "{"
    "\"asset\":{\"version\":\"2.0\"},"
    "\"buffers\":[{\"byteLength\":44}],"
    "\"bufferViews\":["
      "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
      "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}"
    "],"
    "\"accessors\":["
      "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
      "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}"
    "],"
    "\"meshes\":[{"
      "\"primitives\":[{"
        "\"attributes\":{\"POSITION\":0},"
        "\"indices\":1"
      "}]"
    "}],"
    "\"nodes\":[{\"mesh\":0}],"
    "\"scenes\":[{\"nodes\":[0]}],"
    "\"scene\":0"
    "}";

  uint8_t bin[44];
  build_minimal_bin(bin);

  uint32_t json_len = (uint32_t)strlen(json);
  uint32_t json_len_padded = pad4_u32(json_len);
  uint32_t bin_len = 44;
  uint32_t bin_len_padded = pad4_u32(bin_len);

  uint32_t total_len = 12u + 8u + json_len_padded + 8u + bin_len_padded;

  uint8_t* glb = (uint8_t*)malloc(total_len);
  TEST_ASSERT_NOT_NULL(glb);
  memset(glb, 0, total_len);

  // header
  write_u32_le(glb + 0, 0x46546C67u); // 'glTF'
  write_u32_le(glb + 4, 2u);          // version
  write_u32_le(glb + 8, total_len);   // length

  uint32_t off = 12;

  // JSON chunk
  write_u32_le(glb + off + 0, json_len_padded);
  write_u32_le(glb + off + 4, 0x4E4F534Au); // 'JSON'
  off += 8;
  memcpy(glb + off, json, json_len);
  for (uint32_t i = json_len; i < json_len_padded; i++) glb[off + i] = 0x20; // pad with spaces
  off += json_len_padded;

  // BIN chunk
  write_u32_le(glb + off + 0, bin_len_padded);
  write_u32_le(glb + off + 4, 0x004E4942u); // 'BIN\0'
  off += 8;
  memcpy(glb + off, bin, bin_len);
  off += bin_len_padded;

  TEST_ASSERT_EQUAL_UINT32(total_len, off);

  gltf_doc* doc = NULL;
  gltf_error err = {0};
  gltf_result rc = gltf_load_glb_bytes(glb, (size_t)total_len, &doc, &err);
  if (rc != GLTF_OK) {
    char buf[512];
    snprintf(buf, sizeof(buf),
             "gltf_load_glb_bytes failed rc=%d msg=%s path=%s line=%d col=%d",
             rc,
             err.message ? err.message : "(null)",
             err.path ? err.path : "(null)",
             err.line, err.col);
    TEST_FAIL_MESSAGE(buf);
  }

  assert_minimal_triangle_loaded(doc);

  gltf_free(doc);
  free(glb);
}
