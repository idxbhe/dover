cmake_minimum_required(VERSION 3.20)
project(dover LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)
set(MINHOOK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  minhook
  URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.3.zip
)
FetchContent_Populate(minhook)

FetchContent_Declare(
  imgui
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.6.zip
)
FetchContent_Populate(imgui)

FetchContent_Declare(
  md4c
  URL https://github.com/mity/md4c/archive/refs/tags/release-0.5.2.zip
)
FetchContent_Populate(md4c)

FetchContent_Declare(
  imgui_md
  URL https://github.com/mekhontsev/imgui_md/archive/refs/heads/main.zip
)
FetchContent_Populate(imgui_md)

set(FT_REQUIRE_ZLIB OFF CACHE BOOL "Disable ZLIB" FORCE)
set(FT_REQUIRE_BZIP2 OFF CACHE BOOL "Disable BZIP2" FORCE)
set(FT_REQUIRE_PNG OFF CACHE BOOL "Disable PNG" FORCE)
set(FT_REQUIRE_HARFBUZZ OFF CACHE BOOL "Disable HarfBuzz" FORCE)
set(FT_REQUIRE_BROTLI OFF CACHE BOOL "Disable Brotli" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "Disable ZLIB" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "Disable BZIP2" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "Disable PNG" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Disable HarfBuzz" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "Disable Brotli" FORCE)

FetchContent_Declare(
  freetype
  URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-2.zip
)
FetchContent_MakeAvailable(freetype)

add_library(minhook STATIC
  ${minhook_SOURCE_DIR}/src/buffer.c
  ${minhook_SOURCE_DIR}/src/hook.c
  ${minhook_SOURCE_DIR}/src/trampoline.c
  ${minhook_SOURCE_DIR}/src/hde/hde32.c
  ${minhook_SOURCE_DIR}/src/hde/hde64.c
)

target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

if(MSVC)
  target_compile_options(minhook PRIVATE
    /W0
    /wd4100
    /wd4201
    /wd4206
    /wd4244
    /wd4310
    /wd4701
    /wd4706
  )
endif()

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx9.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
  ${imgui_SOURCE_DIR}/misc/freetype/imgui_freetype.cpp
)

target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_FREETYPE)
target_link_libraries(imgui PUBLIC freetype)

if(MSVC)
  target_compile_options(imgui PRIVATE /W0)
endif()

# MD4C: fast C-based Markdown parser
add_library(md4c STATIC
  ${md4c_SOURCE_DIR}/src/md4c.c
)
target_include_directories(md4c PUBLIC ${md4c_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(md4c PRIVATE /W0)
endif()

# imgui_md: Markdown renderer for Dear ImGui (backed by md4c)
add_library(imgui_md_lib STATIC
  ${imgui_md_SOURCE_DIR}/imgui_md.cpp
)
target_include_directories(imgui_md_lib PUBLIC ${imgui_md_SOURCE_DIR})
target_link_libraries(imgui_md_lib PUBLIC md4c imgui)
if(MSVC)
  target_compile_options(imgui_md_lib PRIVATE /W0)
endif()

add_library(dover_shared STATIC
  src/shared/config.cpp
  src/shared/ipc.cpp
  src/shared/process.cpp
  src/shared/log.cpp
)

target_include_directories(dover_shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(dover_launcher src/launcher/main.cpp)
target_link_libraries(dover_launcher PRIVATE dover_shared)

add_executable(dover_injector src/injector/main.cpp)
target_link_libraries(dover_injector PRIVATE dover_shared)

add_library(dover_overlay SHARED
  src/overlay/dllmain.cpp
  src/overlay/dx9_hook.cpp
  src/overlay/dx11_hook.cpp
  src/overlay/hook_utils.cpp
  src/overlay/overlay_ui.cpp
  src/overlay/overlay_runtime.cpp
  src/overlay/notes/manager.cpp
  src/overlay/notes/layout.cpp
  src/overlay/notes/formatter.cpp
  src/overlay/notes/style.cpp
)

target_include_directories(dover_overlay PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_md_SOURCE_DIR}
  ${md4c_SOURCE_DIR}/src
)

target_link_libraries(dover_overlay PRIVATE dover_shared d3d9 minhook imgui imgui_md_lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_target_properties(dover_launcher PROPERTIES OUTPUT_NAME "dover_launcher")
  set_target_properties(dover_injector PROPERTIES OUTPUT_NAME "dover_injector64")
  set_target_properties(dover_overlay PROPERTIES OUTPUT_NAME "dover_overlay64")
else()
  set_target_properties(dover_launcher PROPERTIES OUTPUT_NAME "dover_launcher32")
  set_target_properties(dover_injector PROPERTIES OUTPUT_NAME "dover_injector32")
  set_target_properties(dover_overlay PROPERTIES OUTPUT_NAME "dover_overlay32")
endif()
