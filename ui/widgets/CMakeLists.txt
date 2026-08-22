# Widgets: the pieces screens are assembled from.
#
# Between `attadipa_ui` (tokens, no LVGL) and the applications (screens). It
# exists because a battery gauge is a drawing rather than a number, and a
# drawing written in an application is a drawing written once per application.
#
# What it links is the whole argument: LVGL, the tokens, the generated masks and
# l10n — and **not** `attadipa_platform`. A widget receives a `Metrics` and a
# `Theme`; it cannot ask which panel, which is the same rule the screens above
# it live under and the reason `attadipa_ui` has no link to platform either.
add_library(attadipa_widgets STATIC
    src/battery.cpp
    src/status_strip.cpp
)
add_library(attadipa::widgets ALIAS attadipa_widgets)

target_include_directories(attadipa_widgets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_widgets PUBLIC
    attadipa_core
    attadipa_l10n
    attadipa_ui
    attadipa_images
    lvgl
)
