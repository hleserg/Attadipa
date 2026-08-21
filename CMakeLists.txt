cmake_minimum_required(VERSION 3.20)
project(firefly LANGUAGES CXX)

# Host-native build. ESP-IDF is deliberately not involved here: the bulk of
# Firefly's logic must compile and run on a desktop so it can be tested without
# hardware, and so UI work does not wait on a board.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The simulator is a first-class target (final §57) and is still off by default,
# because it is the only part of this build that needs SDL2 and a network fetch
# of LVGL. `cmake -S . -B build && ctest` has to keep working on a machine with
# neither. CI builds both configurations, in two jobs, for that reason.
option(FIREFLY_BUILD_SIMULATOR "Build the desktop simulator (requires SDL2 and LVGL)" OFF)

add_library(firefly_headers INTERFACE)
target_include_directories(firefly_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The layer order below is the architecture, and it is enforced by the link
# lines rather than by review:
#
#   firefly_platform   chips, pins, rails, buses      — apps/ does not link it
#   firefly_core       services and the capability registry
#   firefly_apps       applications                   — links core only
#
# docs/adr/0007-two-capability-layers.md §5. tests/boundary/ asserts that an
# application still cannot include a platform header.
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(apps)

if(FIREFLY_BUILD_SIMULATOR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FireflyLvgl.cmake)
    add_subdirectory(sim)
endif()

enable_testing()
add_subdirectory(tests)
