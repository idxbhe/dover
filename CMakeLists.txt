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
  src/overlay/overlay_runtime.cpp
)

target_link_libraries(dover_overlay PRIVATE dover_shared d3d9 minhook)
