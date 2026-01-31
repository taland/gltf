# cmake/deps_stb.cmake
# Provides: stb::stb target (when GLTF_ENABLE_IMAGES)

include_guard(GLOBAL)

# Option is expected to be defined in the top-level CMakeLists.txt:
# option(GLTF_ENABLE_IMAGES "Enable image decoding via stb" ON)
# option(GLTF_ALLOW_FETCHCONTENT_FALLBACK "Allow downloading deps via FetchContent" ON)

if(GLTF_ENABLE_IMAGES)
  set(_STB_DIR "${CMAKE_CURRENT_LIST_DIR}/../third_party/stb")
  set(_STB_IMAGE_H "${_STB_DIR}/stb_image.h")

  if(EXISTS "${_STB_IMAGE_H}")
    if(NOT TARGET stb::stb)
      add_library(stb::stb INTERFACE IMPORTED)
      set_target_properties(stb::stb PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_STB_DIR}"
      )
    endif()
  else()
    if(GLTF_ALLOW_FETCHCONTENT_FALLBACK)
      include(FetchContent)
      set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

      FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG f1c79c02822848a9bed4315b12c8c8f3761e1296
        GIT_SHALLOW TRUE
      )
      FetchContent_MakeAvailable(stb)

      if(NOT TARGET stb_headers)
        add_library(stb_headers INTERFACE)
        target_include_directories(stb_headers INTERFACE "${stb_SOURCE_DIR}")
      endif()
      if(NOT TARGET stb::stb)
        add_library(stb::stb ALIAS stb_headers)
      endif()
    else()
      message(FATAL_ERROR
        "stb_image.h not found at '${_STB_IMAGE_H}'.\n"
        "Either vendor stb in third_party/stb or set GLTF_ALLOW_FETCHCONTENT_FALLBACK=ON.")
    endif()
  endif()
endif()
