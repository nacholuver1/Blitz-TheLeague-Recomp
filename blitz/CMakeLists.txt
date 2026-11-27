# blitz - ReXGlue Recompiled Project
#
# The SDK package provides rexglue_project and the rexglue_setup_* functions.

cmake_minimum_required(VERSION 3.25)
# The release version: shown in the launcher and compared with GitHub's latest
# release by the updater (src/blitz_updater.cpp); tools/make_release*.ps1/sh pack it.
project(blitz VERSION 1.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (REXSDK_DIR)
  add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
else ()
  find_package(rexglue 0.11.0 REQUIRED CONFIG)
endif ()
rexglue_project(blitz OUTPUT_DIR generated/default)

# Host sources. src/main.cpp stays on the executable.
set(BLITZ_HOST_SOURCES src/blitz_hooks.cpp src/blitz_look.cpp src/blitz_menu.cpp src/blitz_setup.cpp
    src/blitz_updater.cpp src/blitz_sysinfo.cpp)

# OBJECT and never STATIC, an archive drops the REX_EXPORT symbols.
add_library(blitz_common OBJECT ${BLITZ_HOST_SOURCES})
rexglue_setup_host_library(blitz_common)
target_compile_definitions(blitz_common PRIVATE BLITZ_VERSION="${PROJECT_VERSION}"
                           BLITZ_REPO="nacholuver1/Blitz-TheLeague-Recomp")
if (WIN32)
  target_link_libraries(blitz_common PRIVATE winhttp)  # the updater's downloads
  target_link_libraries(blitz_common PRIVATE ole32)  # sysinfo: audio output format
endif ()
if (NOT WIN32)
  # Menu input and the file picker use SDL3 directly on Linux (XInput on Windows).
  target_link_libraries(blitz_common PRIVATE SDL3::SDL3)
endif ()

if (WIN32)
  add_executable(blitz WIN32 src/main.cpp)
else ()
  add_executable(blitz src/main.cpp)
endif ()

rexglue_setup_target(blitz)
target_link_libraries(blitz PRIVATE blitz_common)
