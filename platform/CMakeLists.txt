# The hardware inventory layer.
#
# This is the library apps/ does not link. The separation is the whole
# enforcement mechanism for docs/adr/0007-two-capability-layers.md §5: an
# application that includes attadipa/platform/… fails to build, rather than
# failing review. tests/boundary/ proves it still fails.
add_library(attadipa_platform
    src/board_profiles.cpp
    src/display_info.cpp
    src/hardware_feature.cpp
    src/profile_inventory.cpp
    src/radio_info.cpp
)
add_library(attadipa::platform ALIAS attadipa_platform)

target_include_directories(attadipa_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_platform PUBLIC attadipa_headers)
