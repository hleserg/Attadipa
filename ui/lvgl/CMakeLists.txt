add_library(attadipa_clock_ui clock_face.cpp provision_face.cpp)
add_library(attadipa::clock_ui ALIAS attadipa_clock_ui)

target_include_directories(attadipa_clock_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_clock_ui PUBLIC
    attadipa_apps
    attadipa_ui
    attadipa_fonts
    attadipa_images
    ${ATTADIPA_LVGL_TARGET}
)
