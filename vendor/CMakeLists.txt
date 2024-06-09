set(RAYLIB_VERSION 5.0)
find_package(raylib ${RAYLIB_VERSION} QUIET) # QUIET or REQUIRED
if (NOT raylib_FOUND) # If there's none, fetch and build raylib
  include(FetchContent)
  FetchContent_Declare(
    raylib
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
    URL https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz
  )
  FetchContent_GetProperties(raylib)
  if (NOT raylib_POPULATED) # Have we downloaded raylib yet?
    set(FETCHCONTENT_QUIET NO)
    FetchContent_Populate(raylib)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) # don't build the supplied examples
    add_subdirectory(${raylib_SOURCE_DIR} ${raylib_BINARY_DIR})
  endif()
endif()


add_library(imgui STATIC)
file(GLOB IMGUI_SOURCE_FILES CONFIGURE_DEPENDS imgui/*.cpp)
target_sources(imgui PRIVATE ${IMGUI_SOURCE_FILES})
target_include_directories(imgui PUBLIC imgui/)

add_library(rlImGui STATIC)
target_sources(rlImGui  PRIVATE ./rlImGui/rlImGui.cpp)
target_link_libraries(rlImGui PUBLIC raylib imgui)
target_include_directories(rlImGui PUBLIC ./rlImGui/)
