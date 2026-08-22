# The generated image assets, and the one function that resolves a design token
# and a panel density into one of them.
#
# Separate from `attadipa_ui` for the same reason `attadipa_fonts` is: an entry
# here is an `lv_image_dsc_t`, so this target knows about LVGL, and
# `attadipa_ui` deliberately knows about nothing but its own headers.
#
# 14.1 kB of .rodata for all nine masks — A8 is one byte per pixel and the sizes
# are 33, 39 and 47 px square. Reported by the generator rather than estimated;
# every declaration in `generated/attadipa_images.h` carries its own byte count.
set(ATTADIPA_IMAGE_SOURCES
    generated/attadipa_icon_mesh_33.c
    generated/attadipa_icon_mesh_39.c
    generated/attadipa_icon_mesh_47.c
    generated/attadipa_icon_position_33.c
    generated/attadipa_icon_position_39.c
    generated/attadipa_icon_position_47.c
    generated/attadipa_icon_warning_33.c
    generated/attadipa_icon_warning_39.c
    generated/attadipa_icon_warning_47.c
)

add_library(attadipa_images STATIC attadipa_icons.cpp ${ATTADIPA_IMAGE_SOURCES})

target_include_directories(attadipa_images PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(attadipa_images PUBLIC lvgl attadipa_ui)

# LVGLImage.py emits an include block that falls through to `lvgl/lvgl.h` unless
# told otherwise. Our LVGL is on the include path as `lvgl.h`, the same way the
# generated fonts include it.
target_compile_definitions(attadipa_images PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)

# Generated third-party output, same rule as the fonts: warnings in it belong to
# LVGLImage.py, and making them errors would turn an asset into a reason a
# contributor cannot build the project.
set_source_files_properties(${ATTADIPA_IMAGE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
