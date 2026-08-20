cmake_minimum_required(VERSION 3.20)
project(firefly LANGUAGES CXX)

# Host-native build. ESP-IDF is deliberately not involved here: the bulk of
# Firefly's logic must compile and run on a desktop so it can be tested without
# hardware, and so UI work does not wait on a board.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(firefly_headers INTERFACE)
target_include_directories(firefly_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

enable_testing()
add_subdirectory(tests)
