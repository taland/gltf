// Minimal educational glTF 2.0 loader (C11).
//
// This module provides small internal allocators used by the document.
//
// Responsibilities:
//   - grow-only arena storage for document-owned strings
//   - shared u32 index pool for expanded index data
//
// Notes:
//   - These helpers are internal; public API contracts live in include/gltf/gltf.h.
//   - The arena owns its memory; returned views are offsets into arena storage.


#include "gltf_internal.h"


// ----------------------------------------------------------------------------
// Arena (strings)
// ----------------------------------------------------------------------------
// Simple grow-only byte arena used to store document-owned strings.
//
// Notes:
//   - arena_strdup() stores a NUL-terminated copy; returned gltf_str refers to the
//     bytes (excluding the terminator).
//   - Operations fail (return 0 / invalid) on overflow or allocation failure.


void arena_init(gltf_arena* arena, size_t initial_cap) {
  if (initial_cap == 0) initial_cap = GLTF_ARENA_INITIAL_CAPACITY;
  arena->data = (uint8_t*)malloc(initial_cap);
  arena->size = 0;
  arena->cap = arena->data ? initial_cap : 0;
}

int arena_reserve(gltf_arena* arena, size_t additional_bytes) {
  if (additional_bytes > SIZE_MAX - arena->size) return 0;

  const size_t required = arena->size + additional_bytes;

  if (required > UINT32_MAX) return 0;
  if (required <= arena->cap) return 1;

  size_t new_cap = arena->cap ? arena->cap : GLTF_ARENA_INITIAL_CAPACITY;
  while (new_cap < required) {
    if (new_cap > SIZE_MAX / 2) {
      new_cap = required;
      break;
    }
    new_cap *= 2;
  }

  uint8_t* new_data = (uint8_t*)realloc(arena->data, new_cap);
  if (!new_data) return 0;

  arena->data = new_data;
  arena->cap = new_cap;

  return 1;
}

gltf_str arena_strdup(gltf_arena* arena, const char* s) {
  if (!s) return gltf_str_invalid();

  size_t len = strlen(s);

  if (arena_reserve(arena, len + 1) == 0) return gltf_str_invalid();

  uint32_t off = (uint32_t)arena->size;
  memcpy(&arena->data[arena->size], s, len + 1);
  arena->size += len + 1;
  gltf_str result = { off, (uint32_t)len };

  return result;
}

const char* arena_get_str(const gltf_arena* arena, gltf_str s) {
  if (!gltf_str_is_valid(s)) return NULL;
  if ((size_t)s.off + (size_t)s.len + 1 > arena->size) return NULL;
  return (const char*)(arena->data + s.off);
}


// ----------------------------------------------------------------------------
// Indices (shared u32 pool)
// ----------------------------------------------------------------------------
// Growable array used to store decoded indices as u32.

int indices_reserve(gltf_doc* doc, uint32_t additional) {
  if (additional > UINT32_MAX - doc->indices_count) return 0;

  uint32_t required = doc->indices_count + additional;

  if (required <= doc->indices_cap) return 1;

  uint32_t new_cap = doc->indices_cap ? doc->indices_cap : GLTF_INDICES_INITIAL_CAP;
  while (new_cap < required) {
    if (new_cap > UINT32_MAX / 2) {
      new_cap = required;
      break;
    }
    new_cap *= 2;
    if ((size_t)new_cap > SIZE_MAX / sizeof(uint32_t)) return 0;
  }

  size_t bytes = (size_t)new_cap * sizeof(uint32_t);
  uint32_t* p = (uint32_t*)realloc(doc->indices_u32, bytes);

  if (!p) return 0;

  doc->indices_u32 = p;
  doc->indices_cap = new_cap;
  return 1;
}

int indices_push_u32(gltf_doc* doc, uint32_t v) {
  if (!indices_reserve(doc, 1)) return 0;
  doc->indices_u32[doc->indices_count++] = v;
  return 1;
}
