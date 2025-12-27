#include "gltf/gltf.h"

#include <stdio.h>

int main(int argc, char** argv) {
  const char* path = "examples/sample_01_minimal.gltf";
  if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
    path = argv[1];
  }

  gltf_doc* doc = NULL;
  gltf_error err = {0};

  int rc = gltf_load_file(path, &doc, &err);
  if (rc != GLTF_OK) {
    const char* msg = err.message ? err.message : "unknown error";
    const char* epath = err.path ? err.path : "";
    int line = err.line ? err.line : 1;
    int col = err.col ? err.col : 1;

    fprintf(stderr, "ERROR: %s at %d:%d path=%s\n", msg, line, col, epath);
    return 1;
  }

  printf("asset.version=%s, scenes=%u, nodes=%u, meshes=%u\n",
         gltf_doc_asset_version(doc),
         gltf_doc_scene_count(doc),
         gltf_doc_node_count(doc),
         gltf_doc_mesh_count(doc));

  for (unsigned i = 0; i < gltf_doc_scene_count(doc); ++i) {
    const char* scene_name = gltf_doc_scene_name(doc, i);
    printf("  scene[%u]: name='%s'\n", i, scene_name ? scene_name : "(null)");
  }

  gltf_free(doc);
  return 0;
}
