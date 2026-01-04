// Minimal educational glTF 2.0 loader (C11).
//
// This module owns document lifetime and top-level JSON parsing.
//
// Responsibilities:
//   - load and validate a .gltf JSON document
//   - populate document-owned arrays (scenes/nodes/meshes/primitives/...)
//   - load buffers from external files and data: URIs
//   - free all document-owned memory
//
// Notes:
//   - Public API contracts are documented in include/gltf/gltf.h.
//   - Error reporting uses static literals for message/path.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Internal helpers (fs + error)
// ----------------------------------------------------------------------------

// File system helpers implemented in src/fs.c.
typedef enum gltf_fs_status {
  GLTF_FS_OK = 0,
  GLTF_FS_INVALID,
  GLTF_FS_IO,
  GLTF_FS_OOM,
  GLTF_FS_SIZE_MISMATCH,
  GLTF_FS_TOO_LARGE
} gltf_fs_status;

size_t gltf_fs_dir_len(const char* path);

char* gltf_fs_join_dir_leaf(const char* dir_prefix,
                            size_t dir_len,
                            const char* leaf);

gltf_fs_status gltf_fs_read_file_exact_u32(const char* path,
                                           uint32_t expected_len,
                                           uint8_t** out_data,
                                           uint32_t* out_len);


// ----------------------------------------------------------------------------
// Document lifetime
// ----------------------------------------------------------------------------

// Writes error details (null-safe).

void gltf_set_err(gltf_error* out_err,
                  const char* message,
                  const char* path,
                  int line, int col) {
  if (!out_err) return;
  out_err->message = message;
  out_err->path = path;
  out_err->line = line;
  out_err->col = col;
}

gltf_result gltf_load_file(const char* path,
                           gltf_doc** out_doc,
                           gltf_error* out_err) {
#define GLTF_FAIL(_r, ...)               \
  do {                                   \
    gltf_set_err(out_err, __VA_ARGS__);  \
    yyjson_doc_free(json_doc);           \
    gltf_free(doc);                      \
    return _r;                           \
  } while (0)

#define GLTF_TRY(expr)                   \
  do {                                   \
    gltf_result _r = (expr);             \
    if (_r != GLTF_OK) {                 \
      yyjson_doc_free(json_doc);         \
      gltf_free(doc);                    \
      return _r;                         \
    }                                    \
  } while (0)

  gltf_doc* doc = NULL;
  yyjson_read_err err;
  yyjson_doc* json_doc = NULL;

  if (out_doc) {
    *out_doc = NULL;
  }

  if (!path || !out_doc) {
    gltf_set_err(out_err, "invalid arguments", "root", 1, 1);
    return GLTF_ERR_INVALID;
  }

  json_doc = yyjson_read_file(path, 0, NULL, &err);
  if (!json_doc) {
    GLTF_FAIL(GLTF_ERR_PARSE, err.msg, "root", 1, 1);
  }

  doc = (gltf_doc*)calloc(1, sizeof(gltf_doc));
  if (!doc) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  arena_init(&doc->arena, GLTF_ARENA_INITIAL_CAPACITY);
  if (!doc->arena.data) {
    GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root", 1, 1);
  }

  // Root
  yyjson_val* root = yyjson_doc_get_root(json_doc);
  if (!root || !yyjson_is_obj(root)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root", 1, 1);
  }

  // Default scene
  GLTF_TRY(
    gltf_json_get_i32(
      root,
      "scene",
      GLTF_DOC_DEFAULT_SCENE_INVALID,
      &doc->default_scene,
      "root.scene",
      out_err)
  );

  // Scenes
  yyjson_val* scenes_val = yyjson_obj_get(root, "scenes");
  if (!scenes_val) {
    doc->scene_count = 0;
  } else if (yyjson_is_arr(scenes_val)) {
    doc->scene_count = (unsigned)yyjson_arr_size(scenes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.scenes", 1, 1);
  }

  if (doc->scene_count > 0) {
    doc->scenes = (gltf_scene*)calloc(doc->scene_count, sizeof(gltf_scene));
    if (!doc->scenes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.scenes", 1, 1);
    }

    size_t scene_idx, scene_max;
    yyjson_val* scene_val = NULL;
    yyjson_arr_foreach(scenes_val, scene_idx, scene_max, scene_val) {
      if (!yyjson_is_obj(scene_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.scenes[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_str_opt_dup_arena(
          scene_val,
          "name",
          &doc->arena,
          &doc->scenes[scene_idx].name,
          "root.scenes[].name",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32_index_array_range_opt(
          doc,
          scene_val,
          "nodes",
          &doc->scenes[scene_idx].nodes,
          "root.scenes[].nodes",
          "root.scenes[].nodes[]",
          out_err)
      );
    }
  }

  // Nodes
  yyjson_val* nodes_val = yyjson_obj_get(root, "nodes");
  if (!nodes_val) {
    doc->node_count = 0;
  } else if (yyjson_is_arr(nodes_val)) {
    doc->node_count = (unsigned)yyjson_arr_size(nodes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.nodes", 1, 1);
  }

  if (doc->node_count > 0) {
    doc->nodes = (gltf_node*)calloc(doc->node_count, sizeof(gltf_node));
    if (!doc->nodes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.nodes", 1, 1);
    }

    size_t node_idx, node_max;
    yyjson_val* node_val = NULL;
    yyjson_arr_foreach(nodes_val, node_idx, node_max, node_val) {
      if (!yyjson_is_obj(node_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.nodes[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_str_opt_dup_arena(
          node_val,
          "name",
          &doc->arena,
          &doc->nodes[node_idx].name,
          "root.nodes[].name",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_i32(
          node_val,
          "mesh",
          -1,
          &doc->nodes[node_idx].mesh,
          "root.nodes[].mesh",
          out_err)
      );
    }
  }

  // Meshes
  yyjson_val* meshes_val = yyjson_obj_get(root, "meshes");
  if (!meshes_val) {
    doc->mesh_count = 0;
  } else if (yyjson_is_arr(meshes_val)) {
    doc->mesh_count = (unsigned)yyjson_arr_size(meshes_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.meshes", 1, 1);
  }

  if (doc->mesh_count > 0) {
    doc->meshes = (gltf_mesh*)calloc(doc->mesh_count, sizeof(gltf_mesh));
    if (!doc->meshes) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.meshes", 1, 1);
    }

    size_t mesh_idx, mesh_max;
    yyjson_val* mesh_val = NULL;
    yyjson_arr_foreach(meshes_val, mesh_idx, mesh_max, mesh_val) {
      if (!yyjson_is_obj(mesh_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_str_opt_dup_arena(
          mesh_val,
          "name",
          &doc->arena,
          &doc->meshes[mesh_idx].name,
          "root.meshes[].name",
          out_err)
      );

      uint32_t primitive_first = doc->primitive_count;
      uint32_t primitive_count = 0;

      yyjson_val* primitives_val = yyjson_obj_get(mesh_val, "primitives");
      if (!primitives_val) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.meshes[].primitives", 1, 1);
      } else if (!yyjson_is_arr(primitives_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.meshes[].primitives", 1, 1);
      }

      size_t _primitive_count = yyjson_arr_size(primitives_val);
      if (_primitive_count > UINT32_MAX) {
        GLTF_FAIL(GLTF_ERR_PARSE, "too many primitives", "root.meshes[].primitives", 1, 1);
      }
      primitive_count = (uint32_t)_primitive_count;

      if (primitive_count > 0) {
        size_t new_count = (size_t)doc->primitive_count + (size_t)primitive_count;

        if (new_count > SIZE_MAX / sizeof(gltf_primitive)) {
          GLTF_FAIL(GLTF_ERR_IO, "too many primitives", "root.meshes[].primitives", 1, 1);
        }

        size_t prim_size = new_count * sizeof(gltf_primitive);
        gltf_primitive* primitives = (gltf_primitive*)realloc(doc->primitives, prim_size);
        if (!primitives) {
          GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.meshes[].primitives", 1, 1);
        }
        memset(&primitives[doc->primitive_count], 0, primitive_count * sizeof(gltf_primitive));
        doc->primitives = primitives;

        size_t prim_idx, prim_max;
        yyjson_val* prim_val = NULL;
        yyjson_arr_foreach(primitives_val, prim_idx, prim_max, prim_val) {
          if (!yyjson_is_obj(prim_val)) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[].primitives[]", 1, 1);
          }

          yyjson_val* attributes_val = yyjson_obj_get(prim_val, "attributes");
          if (!attributes_val) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.meshes[].primitives[].attributes", 1, 1);
          }
          if (!yyjson_is_obj(attributes_val)) {
            GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.meshes[].primitives[].attributes", 1, 1);
          }


          uint32_t attr_first = doc->prim_attr_count;
          uint32_t attr_count = 0;

          // pass 1: count known attrs
          uint32_t known = 0;

          size_t attributes_idx, attributes_max;
          yyjson_val *key, *val;
          yyjson_obj_foreach(attributes_val, attributes_idx, attributes_max, key, val) {
            uint32_t set = 0;
            gltf_attr_semantic sem = gltf_parse_semantic(yyjson_get_str(key), &set);
            if (sem == GLTF_ATTR_UNKNOWN) continue;
            known++;
          }

          if (known > 0) {
            // reserve once
            size_t new_count = (size_t)doc->prim_attr_count + (size_t)known;
            if (new_count > SIZE_MAX / sizeof(gltf_prim_attr)) {
              GLTF_FAIL(GLTF_ERR_IO, "too many primitive attributes", "root.meshes[].primitives", 1, 1);
            }
            size_t prim_attr_size = new_count * sizeof(gltf_prim_attr);
            gltf_prim_attr* prim_attrs = (gltf_prim_attr*)realloc(doc->prim_attrs, prim_attr_size);
            if (!prim_attrs) {
              GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.meshes[].primitives", 1, 1);
            }
            doc->prim_attrs = prim_attrs;

            yyjson_obj_foreach(attributes_val, attributes_idx, attributes_max, key, val) {
              uint32_t out_set_index;
              gltf_attr_semantic semantic = gltf_parse_semantic(
                yyjson_get_str(key),
                &out_set_index
              );

              if (semantic == GLTF_ATTR_UNKNOWN) {
                // Ignore unknown semantics.
                continue;
              }
              if (!yyjson_is_uint(val)) {
                GLTF_FAIL(GLTF_ERR_PARSE, "must be unsigned integer", "root.meshes[].primitives[].attributes[]", 1, 1);
              }

              gltf_prim_attr attr;
              attr.semantic = semantic;
              attr.set_index = out_set_index;
              attr.accessor_index = (uint32_t)yyjson_get_uint(val);            
              doc->prim_attrs[doc->prim_attr_count++] = attr;

              attr_count++;
            }
          }

          uint32_t primitive_idx = primitive_first + prim_idx;

          GLTF_TRY(
            gltf_json_get_i32(
              prim_val,
              "indices",
              -1,
              &doc->primitives[primitive_idx].indices_accessor,
              "root.meshes[].primitives[].indices",
              out_err)
          );
          
          int32_t mode_;
          GLTF_TRY(
            gltf_json_get_i32(
              prim_val,
              "mode",
              GLTF_PRIM_TRIANGLES,
              &mode_,
              "root.meshes[].primitives[].mode",
              out_err)
          );
          if (mode_ < 0 || mode_ > 6) {
            GLTF_FAIL(GLTF_ERR_PARSE, "invalid primitive mode", "root.meshes[].primitives[].mode", 1, 1);
          }
          doc->primitives[primitive_idx].mode = (gltf_prim_mode)mode_;


          doc->primitives[primitive_idx].attributes_first = attr_first;
          doc->primitives[primitive_idx].attributes_count = attr_count;
        }
      }

      doc->meshes[mesh_idx].primitive_first = primitive_first;
      doc->meshes[mesh_idx].primitive_count = primitive_count;
      doc->primitive_count += primitive_count;
    }
  }

  // Accessors
  yyjson_val* accessors_val = yyjson_obj_get(root, "accessors");
  if (!accessors_val) {
    doc->accessor_count = 0;
  } else if (yyjson_is_arr(accessors_val)) {
    doc->accessor_count = (unsigned)yyjson_arr_size(accessors_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.accessors", 1, 1);
  }

  if (doc->accessor_count > 0) {
    doc->accessors = (gltf_accessor*)calloc(doc->accessor_count, sizeof(gltf_accessor));
    if (!doc->accessors) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.accessors", 1, 1);
    }

    size_t accessor_idx, accessor_max;
    yyjson_val* accessor_val = NULL;
    yyjson_arr_foreach(accessors_val, accessor_idx, accessor_max, accessor_val) {
      if (!yyjson_is_obj(accessor_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.accessors[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_i32(
          accessor_val,
          "bufferView",
          -1,
          &doc->accessors[accessor_idx].buffer_view,
          "root.accessors[].bufferView",
          out_err)
      );

      if (doc->accessors[accessor_idx].buffer_view < 0) {
        // byteOffset defaults to 0.
        GLTF_TRY(
          gltf_json_get_u32(
            accessor_val,
            "byteOffset",
            0,
            &doc->accessors[accessor_idx].byte_offset,
            "root.accessors[].byteOffset",
            out_err)
        );
      }

      GLTF_TRY(
        gltf_json_get_u32_req(
          accessor_val,
          "componentType",
          &doc->accessors[accessor_idx].component_type,
          "root.accessors[].componentType",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32_req(
          accessor_val,
          "count",
          &doc->accessors[accessor_idx].count,
          "root.accessors[].count",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_accessor_type_required(
          accessor_val,
          "type",
          &doc->accessors[accessor_idx].type,
          "root.accessors[].type",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_bool(
          accessor_val,
          "normalized",
          0,
          &doc->accessors[accessor_idx].normalized,
          "root.accessors[].normalized",
          out_err)
      );
    }
  }

  // BufferViews
  yyjson_val* buffer_views_val = yyjson_obj_get(root, "bufferViews");
  if (!buffer_views_val) {
    doc->buffer_view_count = 0;
  } else if (yyjson_is_arr(buffer_views_val)) {
    doc->buffer_view_count = (unsigned)yyjson_arr_size(buffer_views_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.bufferViews", 1, 1);
  }

  if (doc->buffer_view_count > 0) {
    doc->buffer_views = (gltf_buffer_view*)calloc(doc->buffer_view_count, sizeof(gltf_buffer_view));
    if (!doc->buffer_views) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.bufferViews", 1, 1);
    }

    size_t buffer_view_idx, buffer_view_max;
    yyjson_val* buffer_view_val = NULL;
    yyjson_arr_foreach(buffer_views_val, buffer_view_idx, buffer_view_max, buffer_view_val) {
      if (!yyjson_is_obj(buffer_view_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.bufferViews[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_u32_req(
          buffer_view_val,
          "buffer",
          &doc->buffer_views[buffer_view_idx].buffer,
          "root.bufferViews[].buffer",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32(
          buffer_view_val,
          "byteLength",
          0,
          &doc->buffer_views[buffer_view_idx].byte_length,
          "root.bufferViews[].byteLength",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32_req(
          buffer_view_val,
          "byteOffset",
          &doc->buffer_views[buffer_view_idx].byte_offset,
          "root.bufferViews[].byteOffset",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32(
          buffer_view_val,
          "byteStride",
          0,
          &doc->buffer_views[buffer_view_idx].byte_stride,
          "root.bufferViews[].byteStride",
          out_err)
      );

      GLTF_TRY(
        gltf_json_get_u32(
          buffer_view_val,
          "target",
          0,
          &doc->buffer_views[buffer_view_idx].target,
          "root.bufferViews[].target",
          out_err)
      );
    }
  }

  // Buffers
  yyjson_val* buffers_val = yyjson_obj_get(root, "buffers");
  if (!buffers_val) {
    doc->buffer_count = 0;
  } else if (yyjson_is_arr(buffers_val)) {
    doc->buffer_count = (unsigned)yyjson_arr_size(buffers_val);
  } else {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be array", "root.buffers", 1, 1);
  }

  if (doc->buffer_count > 0) {
    doc->buffers = (gltf_buffer*)calloc(doc->buffer_count, sizeof(gltf_buffer));
    if (!doc->buffers) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers", 1, 1);
    }

    size_t buffer_idx, buffer_max;
    yyjson_val* buffer_val = NULL;
    yyjson_arr_foreach(buffers_val, buffer_idx, buffer_max, buffer_val) {
      if (!yyjson_is_obj(buffer_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be object", "root.buffers[]", 1, 1);
      }

      GLTF_TRY(
        gltf_json_get_u32_req(
          buffer_val,
          "byteLength",
          &doc->buffers[buffer_idx].byte_length,
          "root.buffers[].byteLength",
          out_err)
      );

      yyjson_val* uri_val = yyjson_obj_get(buffer_val, "uri");
      if (!uri_val) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be present", "root.buffers[].uri", 1, 1);
      } else if (!yyjson_is_str(uri_val)) {
        GLTF_FAIL(GLTF_ERR_PARSE, "must be string", "root.buffers[].uri", 1, 1);
      } else {
        const char* uri = yyjson_get_str(uri_val);
        gltf_str v = arena_strdup(&doc->arena, uri);
        if (!gltf_str_is_valid(v)) {
          GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].uri", 1, 1);
        }
        doc->buffers[buffer_idx].uri = v;

        if (strncmp(uri, "data:", 5) != 0) {
          // External file

          size_t dir_len = gltf_fs_dir_len(path);
          char* full = gltf_fs_join_dir_leaf(path, dir_len, uri);
          if (!full) {
            GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].uri", 1, 1);
          }

          uint32_t actual_len = 0;
          uint8_t* data = NULL;
          gltf_fs_status st = gltf_fs_read_file_exact_u32(full,
                                                          doc->buffers[buffer_idx].byte_length,
                                                          &data,
                                                          &actual_len);
          free(full);

          switch (st) {
          case GLTF_FS_OK:
            doc->buffers[buffer_idx].data = data;
            break;
          case GLTF_FS_SIZE_MISMATCH:
            GLTF_FAIL(GLTF_ERR_PARSE, "buffer file size does not match byteLength", "root.buffers[].byteLength", 1, 1);
            break;
          case GLTF_FS_OOM:
            GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.buffers[].byteLength", 1, 1);
            break;
          case GLTF_FS_TOO_LARGE:
            GLTF_FAIL(GLTF_ERR_PARSE, "buffer file too large", "root.buffers[].uri", 1, 1);
            break;
          default:
            GLTF_FAIL(GLTF_ERR_IO, "failed to read buffer file", "root.buffers[].uri", 1, 1);
            break;
          }
        } else {
          // Data URI (base64)

          uint8_t* bytes = NULL;
          uint32_t out_len = 0;
          GLTF_TRY(gltf_decode_data_uri(uri, doc->buffers[buffer_idx].byte_length, &bytes, &out_len, out_err));
          doc->buffers[buffer_idx].data = bytes;
        }
      }
    }
  }

  yyjson_val* asset_val = yyjson_obj_get(root, "asset");
  if (!asset_val || !yyjson_is_obj(asset_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and an object", "root.asset", 1, 1);
  }

  yyjson_val* version_val = yyjson_obj_get(asset_val, "version");
  if (!version_val || !yyjson_is_str(version_val)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "must be present and a string", "root.asset.version", 1, 1);
  }

  const char* version = yyjson_get_str(version_val);
  if (strlen(version) >= sizeof(doc->asset_version)) {
    GLTF_FAIL(GLTF_ERR_PARSE, "too long", "root.asset.version", 1, 1);
  }
  (void)strncpy(doc->asset_version, version, sizeof(doc->asset_version) - 1);
  doc->asset_version[sizeof(doc->asset_version) - 1] = '\0';

  yyjson_val* generator_val = yyjson_obj_get(asset_val, "generator");
  if (generator_val && yyjson_is_str(generator_val)) {
    const char* generator = yyjson_get_str(generator_val);
    doc->asset_generator = arena_strdup(&doc->arena, generator);
    if (!gltf_str_is_valid(doc->asset_generator)) {
      GLTF_FAIL(GLTF_ERR_IO, "out of memory", "root.asset.generator", 1, 1);
    }
  }

  yyjson_doc_free(json_doc);
  json_doc = NULL;

  *out_doc = doc;
  gltf_set_err(out_err, NULL, NULL, 0, 0);
  return GLTF_OK;

#undef GLTF_TRY
#undef GLTF_FAIL
}

void gltf_free(gltf_doc* doc) {
  if (doc) {
    free(doc->scenes);
    free(doc->nodes);
    free(doc->meshes);
    free(doc->primitives);
    if (doc->buffers) {
      for (uint32_t i = 0; i < doc->buffer_count; i++) {
        free(doc->buffers[i].data);
      }
    }
    free(doc->buffers);
    free(doc->buffer_views);
    free(doc->accessors);
    free(doc->indices_u32);
    free(doc->arena.data);
    free(doc);
  }
}
