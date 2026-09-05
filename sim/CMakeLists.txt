# The desktop simulator.
#
# This is the composition root: it is allowed to see both layers, exactly as
# main() on a device is. Every other target in this build sees one of them.
find_package(SDL2 REQUIRED)

# The screens, and the two keys that drive them, in a library rather than in
# the executable.
#
# `main.cpp` is a `main()`: nothing can link it, so nothing could drive a key
# through LVGL's own event path and see what the screen did. That is not a
# theoretical gap — the navigation readout was built in `main.cpp` and its
# theme key answered by an `if` ladder in the same file, the ladder did not
# mention it, and no test in this repository was able to press a key and
# notice (#432). Everything a test has to reach lives here; the executable
# below is the composition root, the option parsing and the SDL window, which
# are the parts a test cannot have anyway.
add_library(attadipa_sim_screens
    boot_screen.cpp
    clock_screen.cpp
    diagnostic_screen.cpp
    labels.cpp
    nav_screen.cpp
    review_keys.cpp
)

target_include_directories(attadipa_sim_screens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(attadipa_sim_screens PUBLIC
    attadipa_platform
    attadipa_core
    attadipa_apps
    attadipa_l10n
    attadipa_ui
    attadipa_clock_ui
    attadipa_fonts
    lvgl
)

add_executable(attadipa_sim
    debug_server.cpp
    main.cpp
    options.cpp
    png_writer.cpp
    remote_input.cpp
    screen_source.cpp
)

# `lvgl` before `SDL2::SDL2` and after everything that calls into it: LVGL is a
# static archive that references SDL from its in-tree window, mouse and keyboard
# drivers, and a static archive only resolves against libraries to its right.
# Naming it here rather than relying on the transitive order out of
# `attadipa_sim_screens` is what keeps SDL to the right of it.
target_link_libraries(attadipa_sim PRIVATE
    attadipa_sim_screens
    attadipa_debug
    lvgl
    SDL2::SDL2
)

# LVGL carries the SDL display, mouse and keyboard drivers in-tree but does not
# link SDL itself — that is left to whoever builds the application, which is us.
message(STATUS "SDL2 ${SDL2_VERSION} for the simulator")
