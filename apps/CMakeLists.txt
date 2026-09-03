# The application layer.
#
# It links attadipa_core and nothing else. In particular it does not link
# attadipa_platform, and that omission is the point: an application cannot
# include attadipa/platform/… because the include directory is not on its
# compile line. tests/boundary/ asserts that this stays true.
add_library(attadipa_apps
    src/app_manifest.cpp
    src/clock.cpp
    src/navigation.cpp
    src/provisioning.cpp
)
add_library(attadipa::apps ALIAS attadipa_apps)

target_include_directories(attadipa_apps PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# attadipa_l10n is PUBLIC: an application's whole job is to put words on a
# screen, so tr() has to reach anything that links apps. attadipa_core is not
# given it, and that asymmetry is docs/adr/0010-localization.md §4.
target_link_libraries(attadipa_apps PUBLIC attadipa_core attadipa_l10n)
