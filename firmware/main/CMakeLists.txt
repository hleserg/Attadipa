set(ATTADIPA_MAIN_SOURCES "attadipa_main.cpp" "waveshare_board.cpp")
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_MAIN_SOURCES "watch_control.cpp")
endif()
idf_component_register(SRCS ${ATTADIPA_MAIN_SOURCES} INCLUDE_DIRS ".")

# The host libraries are *linked*, not copied. These are the same CMakeLists the
# desktop build and the host tests use, so a source file added to core/ reaches
# the firmware without anybody remembering to add it in a second place — and a
# library that stops compiling for xtensa breaks this build rather than being
# discovered later.
set(ATTADIPA_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)

# Defined at the repository root for the host build; the device build is not
# under that root, so it is defined here too. One interface target with one
# include directory — if it grows past that, move it to a file both can include.
add_library(attadipa_headers INTERFACE)
target_include_directories(attadipa_headers INTERFACE ${ATTADIPA_ROOT}/include)

# Order follows the architecture: platform, then core, then what sits beside it.
# ui/ and apps/ are absent on purpose — they need LVGL, which is T-166.
set(ATTADIPA_LAYERS platform core link l10n)
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_LAYERS debug)
endif()
foreach(layer ${ATTADIPA_LAYERS})
    add_subdirectory(${ATTADIPA_ROOT}/${layer} ${CMAKE_CURRENT_BINARY_DIR}/attadipa_${layer})
endforeach()

# main/ links attadipa_platform, and this is the one place in the tree allowed
# to. The rule in ADR-0007 §5 is that an *application* must not learn which chip
# answered; main/ is the composition root, which is precisely where the board is
# chosen and handed to everything above it. tests/boundary/ enforces the rule
# where it applies, which is apps/.
set(ATTADIPA_DIRECT_LIBRARIES attadipa_core attadipa_platform attadipa_link
                              attadipa_l10n)
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_DIRECT_LIBRARIES attadipa_debug)
endif()
target_link_libraries(${COMPONENT_LIB} PRIVATE ${ATTADIPA_DIRECT_LIBRARIES})

# Transitive dependencies can make a removed direct link look healthy (link,
# for example, also brings core). Keep the composition-root contract explicit;
# the ELF check in CI separately proves that each library contributed code.
get_target_property(_attadipa_firmware_links ${COMPONENT_LIB} LINK_LIBRARIES)
foreach(_required_library ${ATTADIPA_DIRECT_LIBRARIES})
    if(NOT _required_library IN_LIST _attadipa_firmware_links)
        message(FATAL_ERROR "Firmware must directly link ${_required_library}")
    endif()
endforeach()
