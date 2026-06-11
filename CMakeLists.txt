cmake_minimum_required(VERSION 3.20...3.31)

# Supresi total warning dari dependency luar (seperti FreeType)
set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW) # Fix timestamp warning untuk FetchContent
endif()
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD) # Allow FetchContent_Populate for manual library definition
endif()

project(dover LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable MSVC Whole Program Optimization / Link-Time Code Generation (LTCG)
if(POLICY CMP0069)
  cmake_policy(SET CMP0069 NEW)
endif()
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT error)
if(ipo_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
endif()

if(MSVC)
  # Modern MSVC 2026 Flags:
  # /MP                 - Multiprocessor compilation (fast)
  # /permissive-        - Standards conformance mode
  # /EHsc               - C++ exception handling
  # /Zc:preprocessor    - Strict compliant preprocessor
  # /Zc:__cplusplus     - Standard compliant __cplusplus macro
  # /Zi                 - Generate debug symbols (PDB)
  # /Ob2                - Aggressive inline optimization
  add_compile_options(/MP /permissive- /EHsc /Zc:preprocessor /Zc:__cplusplus /Zi /Ob2)
  add_link_options(/DEBUG /OPT:REF /OPT:ICF)

  option(DOVER_ENABLE_ASAN "Enable AddressSanitizer" OFF)
  if(DOVER_ENABLE_ASAN)
    add_compile_options(/fsanitize=address)
  endif()

  option(DOVER_ENABLE_ANALYZE "Enable MSVC Static Code Analysis" OFF)
  if(DOVER_ENABLE_ANALYZE)
    add_compile_options(/analyze /analyze:WX-)
  endif()

  option(DOVER_PGO_INSTRUMENT "Enable PGO Instrumentation" OFF)
  option(DOVER_PGO_OPTIMIZE "Enable PGO Optimization" OFF)
  
  if(DOVER_PGO_INSTRUMENT)
    add_link_options(/GENPROFILE)
  elseif(DOVER_PGO_OPTIMIZE)
    add_link_options(/USEPROFILE)
  endif()
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
  URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.6-docking.zip
)
FetchContent_Populate(imgui)

FetchContent_Declare(
  md4c
  URL https://github.com/mity/md4c/archive/refs/tags/release-0.5.2.zip
)
FetchContent_Populate(md4c)

FetchContent_Declare(
  imgui_md
  URL https://github.com/mekhontsev/imgui_md/archive/b9f86beda0cd657b4d07bd9ce88dd05c9088384f.zip
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

if(MSVC AND TARGET freetype)
  target_compile_options(freetype PRIVATE /W0)
endif()

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

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_sources(imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
endif()

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
  src/shared/storage.cpp
  src/shared/game_storage.cpp
  src/shared/theme.cpp
  src/shared/ui/components/base_window.cpp
  src/shared/settings/settings_window.cpp
  src/shared/settings/app_config.cpp
  src/shared/notes/notes_manager.cpp
  src/shared/notes/notes_layout.cpp
  src/shared/notes/notes_formatter.cpp
  src/shared/notes/notes_style.cpp
  src/shared/crosshair/crosshair_window.cpp
  src/shared/assets/asset_storage.cpp
  src/shared/input_mapper.cpp
  src/shared/input/input_window.cpp
  src/shared/renderer.cpp
  src/shared/input_utils.cpp
  src/shared/engine_quirks.cpp
)

target_include_directories(dover_shared PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_md_SOURCE_DIR}
  ${md4c_SOURCE_DIR}/src
)

target_link_libraries(dover_shared PUBLIC imgui imgui_md_lib xinput)

add_executable(dover_launcher WIN32 src/launcher/main.cpp src/launcher/launcher.rc)
target_include_directories(dover_launcher PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(dover_launcher PRIVATE dover_shared imgui d3d11)

add_executable(dover_injector src/injector/main.cpp)
target_link_libraries(dover_injector PRIVATE dover_shared)

add_library(dover_overlay SHARED
  src/overlay/dllmain.cpp
  src/overlay/dx9_hook.cpp
  src/overlay/dx11_hook.cpp
  src/overlay/hook_utils.cpp
  src/overlay/input_hook.cpp
  src/overlay/overlay_ui.cpp
  src/overlay/overlay_runtime.cpp
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_sources(dover_overlay PRIVATE src/overlay/dx12_hook.cpp)
endif()

target_include_directories(dover_overlay PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_md_SOURCE_DIR}
  ${md4c_SOURCE_DIR}/src
)

target_link_libraries(dover_overlay PRIVATE dover_shared d3d11 dxgi d3d9 minhook imgui imgui_md_lib)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_link_libraries(dover_overlay PRIVATE d3d12)
endif()

option(DOVER_STRICT_BUILD "Treat warnings as errors for Dover targets" OFF)
if(DOVER_STRICT_BUILD)
  if(MSVC)
    set(DOVER_STRICT_FLAGS /WX /W4) 
  else()
    set(DOVER_STRICT_FLAGS -Werror -Wall)
  endif()
  
  foreach(target dover_shared dover_launcher dover_injector dover_overlay)
    if(TARGET ${target})
      target_compile_options(${target} PRIVATE ${DOVER_STRICT_FLAGS})
    endif()
  endforeach()
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set_target_properties(dover_launcher PROPERTIES OUTPUT_NAME "launcher")
  set_target_properties(dover_injector PROPERTIES OUTPUT_NAME "injector64")
  set_target_properties(dover_overlay PROPERTIES OUTPUT_NAME "overlay64")
else()
  set_target_properties(dover_launcher PROPERTIES OUTPUT_NAME "launcher32")
  set_target_properties(dover_injector PROPERTIES OUTPUT_NAME "injector32")
  set_target_properties(dover_overlay PROPERTIES OUTPUT_NAME "overlay32")
endif()

install(TARGETS dover_launcher dover_injector dover_overlay
        RUNTIME DESTINATION .
        LIBRARY DESTINATION .
        ARCHIVE DESTINATION lib)
