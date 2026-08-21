# The application layer.
#
# It links firefly_core and nothing else. In particular it does not link
# firefly_platform, and that omission is the point: an application cannot
# include firefly/platform/… because the include directory is not on its
# compile line. tests/boundary/ asserts that this stays true.
add_library(firefly_apps
    src/app_manifest.cpp
)
add_library(firefly::apps ALIAS firefly_apps)

target_include_directories(firefly_apps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(firefly_apps PUBLIC firefly_core)
