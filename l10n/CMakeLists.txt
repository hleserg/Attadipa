# The string catalogues.
#
# This library sits beside core rather than inside it, and that placement is the
# rule from docs/adr/0010-localization.md §4: **the core does not speak
# English.** Services, drivers and both external protocols emit a code and its
# parameters; the words are chosen at the UI boundary. If firefly_core ever
# links this library, that rule is gone, and it goes quietly — which is why
# tests/boundary/ compiles a fixture against core and requires it to fail.
#
# It links nothing. A catalogue is data and a plural rule is arithmetic; neither
# needs to know what a board is.
add_library(firefly_l10n
    src/catalogues.cpp     # GENERATED — tools/l10n/gen_strings.py
    src/locale.cpp
    src/tr.cpp
)
add_library(firefly::l10n ALIAS firefly_l10n)

target_include_directories(firefly_l10n PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(firefly_l10n PUBLIC firefly_headers)
