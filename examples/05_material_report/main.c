#include "gltf/gltf.h"

#include <stdint.h>
#include <stdio.h>

static const char* image_kind_to_string(gltf_image_kind kind) {
  switch (kind) {
    case GLTF_IMAGE_URI:
      return "URI";
    case GLTF_IMAGE_DATA_URI:
      return "DATA_URI";
    case GLTF_IMAGE_BUFFER_VIEW:
      return "BUFFER_VIEW";
    case GLTF_IMAGE_NONE:
      return "NONE";
    default:
      return "UNKNOWN";
  }
}

static void print_texture_details(const gltf_doc* doc, int32_t texture_index) {
  if (texture_index < 0) return;
  const gltf_texture* tex = NULL;
  if (!gltf_doc_texture(doc, (uint32_t)texture_index, &tex) || !tex) return;

  printf("    texture: %d", texture_index);
  if (tex->source >= 0) {
    printf(" -> image: %d\n", tex->source);
    const gltf_image* img = NULL;
    if (gltf_doc_image(doc, (uint32_t)tex->source, &img) && img) {
      printf("    image.kind: %s\n", image_kind_to_string(img->kind));
      if (img->kind == GLTF_IMAGE_BUFFER_VIEW) {
        printf("    bufferView: %d\n", img->buffer_view);
        if (img->mime_type) {
          printf("    mimeType:  %s\n", img->mime_type);
        }
      }
      if (img->uri) {
        printf("    image.uri: %s\n", img->uri);
      }
      if (img->kind == GLTF_IMAGE_URI) {
        const char* resolved = gltf_image_resolved_uri(doc, (uint32_t)tex->source);
        if (resolved && resolved != img->uri) {
          printf("    resolved:  %s\n", resolved);
        }
      }
    }
  } else {
    printf("\n");
  }

  if (tex->sampler >= 0) {
    const gltf_sampler* samp = NULL;
    if (gltf_doc_sampler(doc, (uint32_t)tex->sampler, &samp) && samp) {
      printf("    sampler: %d (min=%d mag=%d wrapS=%d wrapT=%d)\n",
             tex->sampler,
             samp->min_filter,
             samp->mag_filter,
             samp->wrap_s,
             samp->wrap_t);
    }
  }
}

static void print_texture_section(const gltf_doc* doc,
                                  const char* label,
                                  const gltf_texture_info* info,
                                  int has_extra,
                                  const char* extra_label,
                                  float extra_value) {
  if (info->index < 0) {
    printf("  %s: none\n", label);
    printf("\n");
    return;
  }

  printf("  %s:\n", label);
  printf("    texCoord: %d\n", info->tex_coord);
  if (has_extra) {
    printf("    %s: %.3f\n", extra_label, extra_value);
  }
  print_texture_details(doc, info->index);
  printf("\n");
}


int main(int argc, char** argv) {
  const char* path = "tests/fixtures/05-materials.gltf";
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
    int col  = err.col  ? err.col  : 1;

    fprintf(stderr, "ERROR: %s at %d:%d path=%s\n", msg, line, col, epath);
    return 1;
  }

  uint32_t material_count = gltf_doc_material_count(doc);
  for (uint32_t i = 0; i < material_count; ++i) {
    const gltf_material* mat = NULL;
    if (!gltf_doc_material(doc, i, &mat) || !mat) continue;

    printf("Material[%u] \"%s\"\n", i, mat->name ? mat->name : "<unnamed>");
    printf("  baseColorFactor: (%.3f, %.3f, %.3f, %.3f)\n",
           mat->pbr.base_color_factor[0],
           mat->pbr.base_color_factor[1],
           mat->pbr.base_color_factor[2],
           mat->pbr.base_color_factor[3]);
    printf("  metallicFactor: %.3f\n", mat->pbr.metallic_factor);
    printf("  roughnessFactor: %.3f\n", mat->pbr.roughness_factor);
    printf("  emissiveFactor: (%.3f, %.3f, %.3f)\n\n",
           mat->emissive_factor[0],
           mat->emissive_factor[1],
           mat->emissive_factor[2]);

    print_texture_section(
      doc,
      "baseColorTexture",
      &mat->pbr.base_color_texture,
      0,
      NULL,
      0.0f
    );

    print_texture_section(
      doc,
      "metallicRoughnessTexture",
      &mat->pbr.metallic_roughness_texture,
      0,
      NULL,
      0.0f
    );

    print_texture_section(
      doc,
      "normalTexture",
      &mat->normal_texture.base,
      1,
      "scale",
      mat->normal_texture.scale
    );

    print_texture_section(
      doc,
      "occlusionTexture",
      &mat->occlusion_texture.base,
      1,
      "strength",
      mat->occlusion_texture.strength
    );

    print_texture_section(
      doc,
      "emissiveTexture",
      &mat->emissive_texture,
      0,
      NULL,
      0.0f
    );
  }

  gltf_free(doc);
  return 0;
}
