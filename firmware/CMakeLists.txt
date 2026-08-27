# The device build. The repository root next to this one is host-native on
# purpose (see ../CMakeLists.txt) and stays that way: the simulator and the host
# tests must keep working on a machine with no toolchain. So the ESP-IDF project
# lives here rather than at the root, and the two builds never see each other's
# cache.
#
#   cd firmware && idf.py set-target esp32s3 && idf.py build
#
# ESP-IDF version is pinned — docs/research/DEPENDENCIES.md, "ESP-IDF" row.
cmake_minimum_required(VERSION 3.16)

# ESP-IDF derives PROJECT_VER from `git describe --always --tags --dirty`, which
# picks up the writer gate's claim tags (refs/tags/attadipa-claims/*, pointing at
# main's head). Every build made while any claim is held then carries that
# claimant's token as its version. On 2026-08-28 that stamped one agent's claim
# name onto another agent's board and nearly caused a needless reflash, and it
# signed a bench log with a name that had nothing to do with the image. The
# fallback keeps the build working outside a git checkout.
execute_process(
  COMMAND git describe --always --tags --dirty --exclude "attadipa-claim*"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  OUTPUT_VARIABLE PROJECT_VER
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET)
if(NOT PROJECT_VER)
  set(PROJECT_VER "unknown")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(attadipa)
