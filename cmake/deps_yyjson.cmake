# cmake/deps_yyjson.cmake
# Provides: yyjson::yyjson target

include_guard(GLOBAL)

# Option is expected to be defined in the top-level CMakeLists.txt:
# option(GLTF_USE_VENDORED_YYJSON "Use vendored yyjson from third_party" ON)
# option(GLTF_ALLOW_FETCHCONTENT_FALLBACK "Allow downloading deps via FetchContent" ON)

if(GLTF_USE_VENDORED_YYJSON)
  # Vendored = use local sources, no network.
  set(_YYJSON_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/yyjson")
  set(_YYJSON_C   "${_YYJSON_DIR}/yyjson.c")
  set(_YYJSON_H   "${_YYJSON_DIR}/yyjson.h")

  if(NOT EXISTS "${_YYJSON_C}" OR NOT EXISTS "${_YYJSON_H}")
    message(FATAL_ERROR
      "Vendored yyjson not found at '${_YYJSON_DIR}'.\n"
      "Expected files:\n"
      "  ${_YYJSON_C}\n"
      "  ${_YYJSON_H}\n"
      "Either vendor yyjson there or set GLTF_USE_VENDORED_YYJSON=OFF.")
  endif()

  # Create an internal yyjson target if it doesn't exist yet.
  if(NOT TARGET yyjson)
    add_library(yyjson STATIC "${_YYJSON_C}")
    target_include_directories(yyjson PUBLIC "${_YYJSON_DIR}")
    set_target_properties(yyjson PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      C_EXTENSIONS OFF
    )
  endif()

else()
  # Prefer system/package-provided yyjson if available.
  find_package(yyjson CONFIG QUIET)

  # If no package target, optionally fallback to FetchContent (network).
  if(NOT (TARGET yyjson::yyjson OR TARGET yyjson) AND GLTF_ALLOW_FETCHCONTENT_FALLBACK)
    include(FetchContent)

    # CI stability: don't auto-update already-fetched repos.
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

    set(YYJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YYJSON_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(YYJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
      yyjson
      GIT_REPOSITORY https://github.com/ibireme/yyjson.git
      GIT_TAG 0.10.0
    )
    FetchContent_MakeAvailable(yyjson)
  endif()

  if(NOT (TARGET yyjson::yyjson OR TARGET yyjson))
    message(FATAL_ERROR
      "yyjson not found.\n"
      "Either:\n"
      "  - vendor it and set GLTF_USE_VENDORED_YYJSON=ON, or\n"
      "  - install a yyjson CMake package (find_package), or\n"
      "  - set GLTF_ALLOW_FETCHCONTENT_FALLBACK=ON (requires network).")
  endif()
endif()

# Normalize target name for consumers inside this project.
if(TARGET yyjson::yyjson)
  # OK
elseif(TARGET yyjson)
  add_library(yyjson::yyjson ALIAS yyjson)
else()
  message(FATAL_ERROR "Internal error: yyjson target not found after deps setup.")
endif()
