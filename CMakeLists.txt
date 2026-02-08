cmake_minimum_required(VERSION 3.25)

project(glTF LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

option(GLTF_BUILD_EXAMPLES "Build example executables" ON)
option(GLTF_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)
option(GLTF_BUILD_TESTS "Build unit tests" ON)

# ON  => use vendored yyjson from third_party/yyjson (offline, reproducible)
# OFF => try find_package(yyjson), fallback to FetchContent (depends on your deps.cmake policy)
option(GLTF_USE_VENDORED_YYJSON "Use vendored yyjson from third_party/yyjson" ON)

# When GLTF_USE_VENDORED_YYJSON=OFF, allow downloading dependencies (yyjson) via FetchContent.
# Set OFF for strict/offline CI builds.
option(GLTF_ALLOW_FETCHCONTENT_FALLBACK "Allow downloading deps via FetchContent" ON)

option(GLTF_ENABLE_IMAGES "Enable image decoding via stb" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Dependencies (provides yyjson::yyjson, stb::stb)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps.cmake)

# Put all runtime binaries in ${CMAKE_BINARY_DIR}/bin
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(CMAKE_CONFIGURATION_TYPES)
  foreach(cfg ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${cfg}" cfg_uc)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_uc} "${CMAKE_BINARY_DIR}/bin")
  endforeach()
endif()

add_library(gltf STATIC
  src/base64.c
  src/fs.c
  src/gltf_accessor.c
  src/gltf_decode.c
  src/gltf_doc.c
  src/gltf_memory.c
  src/gltf_parse.c
  src/gltf_primitive.c
  src/gltf_query.c
  src/gltf_util.c
  src/gltf_world.c
  src/gltf_images.c
)

# Do NOT expose yyjson as a public dependency of gltf.
target_link_libraries(gltf PRIVATE yyjson::yyjson)

target_compile_definitions(gltf PRIVATE
  GLTF_ENABLE_IMAGES=$<BOOL:${GLTF_ENABLE_IMAGES}>
)

if(GLTF_ENABLE_IMAGES)
  target_link_libraries(gltf PRIVATE stb::stb)
endif()

target_include_directories(gltf
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(gltf PUBLIC c_std_11)

target_compile_options(gltf
  PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/W4>
    $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<COMPILE_LANGUAGE:C>>:-Wall -Wextra -Wpedantic>
)

if(GLTF_WARNINGS_AS_ERRORS)
  target_compile_options(gltf
    PRIVATE
      $<$<C_COMPILER_ID:MSVC>:/WX>
      $<$<AND:$<NOT:$<C_COMPILER_ID:MSVC>>,$<COMPILE_LANGUAGE:C>>:-Werror>
  )
endif()

if(GLTF_BUILD_EXAMPLES)
  add_executable(gltf_example_01_dump examples/01_dump/main.c)
  target_link_libraries(gltf_example_01_dump PRIVATE gltf)

  add_executable(gltf_example_04_world_trs examples/04_world_trs/main.c)
  target_link_libraries(gltf_example_04_world_trs PRIVATE gltf)

  add_executable(gltf_example_05_material_report examples/05_material_report/main.c)
  target_link_libraries(gltf_example_05_material_report PRIVATE gltf)

  add_executable(gltf_example_06_extract_textures examples/06_extract_textures/main.c)
  target_link_libraries(gltf_example_06_extract_textures PRIVATE gltf)

  add_executable(gltf_example_07_glb_to_gltf examples/07_glb_to_gltf/main.c)
  target_link_libraries(gltf_example_07_glb_to_gltf PRIVATE gltf yyjson::yyjson)
endif()

if(GLTF_BUILD_TESTS)
  enable_testing()

  add_executable(gltf_tests
    tests/test_main.c
    tests/test_01_minimal.c
    tests/test_02_positions_and_indices.c
    tests/test_03_primitives.c
    tests/test_04_world_matrices_minimal_identity.c
    tests/test_05_materials.c
    tests/test_06_images_datauri.c
    tests/test_07_load_glb.c
    third_party/unity/unity.c
  )

  target_link_libraries(gltf_tests PRIVATE gltf)

  target_include_directories(gltf_tests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/third_party/unity
  )

  target_compile_definitions(gltf_tests
    PRIVATE
      GLTF_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}"
  )

  add_test(NAME gltf_tests COMMAND gltf_tests)
endif()
