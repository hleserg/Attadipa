# The desktop simulator.
#
# This is the composition root: it is allowed to see both layers, exactly as
# main() on a device is. Every other target in this build sees one of them.
find_package(SDL2 REQUIRED)

add_executable(firefly_sim
    boot_screen.cpp
    main.cpp
    options.cpp
    png_writer.cpp
)

target_link_libraries(firefly_sim PRIVATE
    firefly_platform
    firefly_core
    firefly_apps
    firefly_l10n
    lvgl
    SDL2::SDL2
)

# LVGL carries the SDL display, mouse and keyboard drivers in-tree but does not
# link SDL itself — that is left to whoever builds the application, which is us.
message(STATUS "SDL2 ${SDL2_VERSION} for the simulator")
