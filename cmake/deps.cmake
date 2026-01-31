# cmake/deps.cmake
# Provides: yyjson::yyjson target, stb::stb target (when GLTF_ENABLE_IMAGES)

include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/deps_yyjson.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/deps_stb.cmake)
