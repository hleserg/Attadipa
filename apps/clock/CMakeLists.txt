# The Clock.
#
# Links `attadipa_apps` (core + l10n), `attadipa_ui` (the design tokens),
# `attadipa_fonts` and LVGL — and **not** `attadipa_platform`. The omission is
# the architecture: this screen cannot include a hardware header because the
# include directory is not on its compile line, so it cannot learn which board
# it is on even by accident. tests/boundary/ already asserts that for
# `attadipa_apps`, and this target inherits it.
#
# Built only when LVGL is available, which today means the simulator. Firmware
# will link exactly this target.
add_library(attadipa_app_clock STATIC
    src/clock_screen.cpp
)

target_include_directories(attadipa_app_clock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_app_clock PUBLIC
    attadipa_apps
    attadipa_ui
    attadipa_fonts
    lvgl
)
