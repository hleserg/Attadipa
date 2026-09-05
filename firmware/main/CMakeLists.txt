# The board is chosen here and nowhere above it (Kconfig.projbuild, ADR-0007 §5).
if(CONFIG_ATTADIPA_BOARD_TWATCH_S3_PLUS)
    set(ATTADIPA_MAIN_SOURCES "attadipa_main.cpp" "twatch_board.cpp"
                              "board_power.cpp")
else()
    set(ATTADIPA_MAIN_SOURCES "attadipa_main.cpp" "waveshare_board.cpp"
                              "physical_input.cpp" "board_power.cpp")
endif()
if(CONFIG_BT_NIMBLE_ENABLED)
    list(APPEND ATTADIPA_MAIN_SOURCES "meshcore_ble.cpp")
endif()
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_MAIN_SOURCES "watch_control.cpp")
endif()
if(CONFIG_ATTADIPA_I2C_PROBE)
    list(APPEND ATTADIPA_MAIN_SOURCES "i2c_probe.cpp")
endif()
if(CONFIG_ATTADIPA_GNSS_BRIDGE)
    list(APPEND ATTADIPA_MAIN_SOURCES "gnss_bridge.cpp")
endif()
# Compiled into every image, feature or not: with the symbol off its three
# functions are the no-ops the header promises, so both board files call them
# unconditionally and neither needs an #if of its own to get wrong.
#
# It used to be behind the Waveshare board guard, which was the build-side half
# of the Kconfig refusal #442 removed. Left there, a T-Watch image with the
# feature on would have failed at the link rather than at the prompt -- the
# outcome that guard existed to prevent, arriving because the guard stayed.
list(APPEND ATTADIPA_MAIN_SOURCES "local_gnss.cpp")
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
# The product Clock now links the same apps/, ui/ and generated fonts as the
# simulator; the board-specific RTC remains here in the composition root.
set(ATTADIPA_LAYERS platform core link l10n ui apps)
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_LAYERS debug)
endif()
if(CONFIG_ATTADIPA_GNSS_LOCAL)
    list(APPEND ATTADIPA_LAYERS gnss)
endif()
foreach(layer ${ATTADIPA_LAYERS})
    add_subdirectory(${ATTADIPA_ROOT}/${layer} ${CMAKE_CURRENT_BINARY_DIR}/attadipa_${layer})
endforeach()

set(ATTADIPA_LVGL_TARGET __idf_lvgl__lvgl)
add_subdirectory(${ATTADIPA_ROOT}/assets/fonts
                 ${CMAKE_CURRENT_BINARY_DIR}/attadipa_fonts)
add_subdirectory(${ATTADIPA_ROOT}/ui/assets
                 ${CMAKE_CURRENT_BINARY_DIR}/attadipa_images)
add_subdirectory(${ATTADIPA_ROOT}/ui/lvgl
                 ${CMAKE_CURRENT_BINARY_DIR}/attadipa_clock_ui)

# main/ links attadipa_platform, and this is the one place in the tree allowed
# to. The rule in ADR-0007 §5 is that an *application* must not learn which chip
# answered; main/ is the composition root, which is precisely where the board is
# chosen and handed to everything above it. tests/boundary/ enforces the rule
# where it applies, which is apps/.
set(ATTADIPA_DIRECT_LIBRARIES attadipa_core attadipa_platform attadipa_link
                              attadipa_l10n attadipa_ui attadipa_apps
                              attadipa_fonts attadipa_clock_ui)
if(CONFIG_ATTADIPA_WATCH_CONTROL)
    list(APPEND ATTADIPA_DIRECT_LIBRARIES attadipa_debug)
endif()
# Gated with the layer above, and for the reason the ELF check exists: a
# library linked into an image that calls nothing in it contributes no code,
# and that check would fail rather than the link.
if(CONFIG_ATTADIPA_GNSS_LOCAL)
    list(APPEND ATTADIPA_DIRECT_LIBRARIES attadipa_gnss)
endif()
target_link_libraries(${COMPONENT_LIB} PRIVATE ${ATTADIPA_DIRECT_LIBRARIES})
target_link_libraries(${COMPONENT_LIB} PRIVATE idf::nvs_flash)
if(CONFIG_BT_NIMBLE_ENABLED)
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::bt)
endif()

# Transitive dependencies can make a removed direct link look healthy (link,
# for example, also brings core). Keep the composition-root contract explicit;
# the ELF check in CI separately proves that each library contributed code.
get_target_property(_attadipa_firmware_links ${COMPONENT_LIB} LINK_LIBRARIES)
foreach(_required_library ${ATTADIPA_DIRECT_LIBRARIES})
    if(NOT _required_library IN_LIST _attadipa_firmware_links)
        message(FATAL_ERROR "Firmware must directly link ${_required_library}")
    endif()
endforeach()
