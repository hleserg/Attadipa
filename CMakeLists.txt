cmake_minimum_required(VERSION 3.20)
project(attadipa LANGUAGES CXX)

# Host-native build. ESP-IDF is deliberately not involved here: the bulk of
# Attadipa's logic must compile and run on a desktop so it can be tested without
# hardware, and so UI work does not wait on a board.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The simulator is a first-class target (final §57) and is still off by default,
# because it is the only part of this build that needs SDL2 and a network fetch
# of LVGL. `cmake -S . -B build && ctest` has to keep working on a machine with
# neither. CI builds both configurations, in two jobs, for that reason.
option(ATTADIPA_BUILD_SIMULATOR "Build the desktop simulator (requires SDL2 and LVGL)" OFF)

add_library(attadipa_headers INTERFACE)
target_include_directories(attadipa_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# The layer order below is the architecture, and it is enforced by the link
# lines rather than by review:
#
#   attadipa_platform   chips, pins, rails, buses      — apps/ does not link it
#   attadipa_core       services and the capability registry
#   attadipa_link       framing, queues and link state — below core, links core
#   attadipa_l10n       the string catalogues          — core does not link it
#   attadipa_apps       applications                   — links core and l10n
#
# docs/adr/0007-two-capability-layers.md §5 for the first boundary and
# docs/adr/0010-localization.md §4 for the second. They point in opposite
# directions and are the same kind of rule: an application must not learn which
# chip answered, and the core must not learn which language will be read.
# tests/boundary/ asserts both, by compiling a fixture that must not build.
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(link)
add_subdirectory(l10n)
add_subdirectory(ui)
add_subdirectory(apps)

if(ATTADIPA_BUILD_SIMULATOR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/AttadipaLvgl.cmake)
    # The generated fonts need LVGL's headers, so they come after it and only
    # when it is present. Firmware will link the same target for the same reason
    # the simulator does: LVGL's built-in Montserrat cannot draw Cyrillic.
    add_subdirectory(assets/fonts)
    add_subdirectory(ui/assets)
    # Widgets, then the applications made of them. Both need LVGL, so both come
    # after it — and neither links attadipa_platform.
    add_subdirectory(ui/widgets)
    add_subdirectory(apps/clock)
    add_subdirectory(sim)
endif()

enable_testing()
add_subdirectory(tests)
