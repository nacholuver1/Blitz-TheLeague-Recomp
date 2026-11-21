# blitz - ReXGlue Recompiled Project
#
# The SDK package provides rexglue_project and the rexglue_setup_* functions.

cmake_minimum_required(VERSION 3.25)
project(blitz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (REXSDK_DIR)
  add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else ()
  find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif ()
rexglue_project(blitz OUTPUT_DIR generated/default)

# Host sources. src/main.cpp stays on the executable.
set(BLITZ_HOST_SOURCES src/blitz_hooks.cpp src/blitz_menu.cpp src/blitz_setup.cpp)

# OBJECT and never STATIC, an archive drops the REX_EXPORT symbols.
add_library(blitz_common OBJECT ${BLITZ_HOST_SOURCES})
rexglue_setup_host_library(blitz_common)

if (WIN32)
  add_executable(blitz WIN32 src/main.cpp)
else ()
  add_executable(blitz src/main.cpp)
endif ()

rexglue_setup_target(blitz)
target_link_libraries(blitz PRIVATE blitz_common)
