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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(attadipa)
