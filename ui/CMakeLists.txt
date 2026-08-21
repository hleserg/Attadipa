# The design tokens (T-009, final §54) — the code half of docs/ui/DESIGN_SYSTEM.md.
#
# It links nothing but the shared header settings, and that is the point. A
# token resolves a Dp to a pixel from a dpi it is *given*; it never asks which
# panel, because an application asking for `space.md` must not be able to learn
# which board answered. The composition root reads DisplayInfo::dpi() and hands
# the number over — the same shape as the capability registry, applied to
# pixels.
#
# So there is deliberately no link to attadipa_platform here, PRIVATE or
# otherwise. Adding one would let a screen reach the board through the tokens.
add_library(attadipa_ui
    src/color.cpp
    src/tokens.cpp
)
add_library(attadipa::ui ALIAS attadipa_ui)

target_include_directories(attadipa_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_ui PUBLIC attadipa_headers)
