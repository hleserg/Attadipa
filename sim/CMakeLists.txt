# The desktop simulator.
#
# This is the composition root: it is allowed to see both layers, exactly as
# main() on a device is. Every other target in this build sees one of them.
find_package(SDL2 REQUIRED)

add_executable(attadipa_sim
    boot_screen.cpp
    clock_host.cpp
    main.cpp
    labels.cpp
    options.cpp
    png_writer.cpp
)

target_link_libraries(attadipa_sim PRIVATE
    attadipa_platform
    attadipa_core
    attadipa_apps
    attadipa_app_clock
    attadipa_l10n
    attadipa_ui
    attadipa_fonts
    lvgl
    SDL2::SDL2
)

# LVGL carries the SDL display, mouse and keyboard drivers in-tree but does not
# link SDL itself — that is left to whoever builds the application, which is us.
message(STATUS "SDL2 ${SDL2_VERSION} for the simulator")
