# The generated UI fonts.
#
# Separate from `attadipa_ui` on purpose: a font here is an `lv_font_t`, so this
# target knows about LVGL, and `attadipa_ui` deliberately knows about nothing but
# its own headers. The design system names a `TypeRole`; this is one of the
# places a role can be resolved into something that draws.
#
# The two display sizes are intentionally full Cyrillic subsets too. The Clock
# only draws digits with them today, but the role is a product font contract,
# not a private numeric asset.

add_library(attadipa_fonts STATIC
  generated/attadipa_nunito_sans_14.c
  generated/attadipa_nunito_sans_16.c
  generated/attadipa_nunito_sans_20.c
  generated/attadipa_nunito_sans_28.c
  generated/attadipa_nunito_sans_64.c
  generated/attadipa_nunito_sans_96.c
)

target_include_directories(attadipa_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(attadipa_fonts PUBLIC ${ATTADIPA_LVGL_TARGET})

# Generated third-party output. Warnings in it are lv_font_conv's to fix, and
# turning them into errors in a strict build would make the font a reason a
# contributor cannot build the project.
target_compile_options(attadipa_fonts PRIVATE -w)
