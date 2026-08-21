# The generated UI fonts.
#
# Separate from `attadipa_ui` on purpose: a font here is an `lv_font_t`, so this
# target knows about LVGL, and `attadipa_ui` deliberately knows about nothing but
# its own headers. The design system names a `TypeRole`; this is one of the
# places a role can be resolved into something that draws.
#
# ~79 kB of .rodata for all four sizes, measured with xtensa `size -A` on the
# generated objects: 13.0 / 15.2 / 19.4 / 31.3 kB at 14, 16, 20 and 28 px.

add_library(attadipa_fonts STATIC
    generated/attadipa_montserrat_14.c
    generated/attadipa_montserrat_16.c
    generated/attadipa_montserrat_20.c
    generated/attadipa_montserrat_28.c
    generated/attadipa_montserrat_num_64.c
    generated/attadipa_montserrat_num_96.c
)

target_include_directories(attadipa_fonts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(attadipa_fonts PUBLIC lvgl)

# Generated third-party output. Warnings in it are lv_font_conv's to fix, and
# turning them into errors in a strict build would make the font a reason a
# contributor cannot build the project.
target_compile_options(attadipa_fonts PRIVATE -w)
