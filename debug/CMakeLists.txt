# The debug channel: screenshots out, input in.
#
# A library rather than something folded into the simulator, for two reasons.
# It has to be testable with no LVGL, no SDL and no socket -- most of the
# failure modes worth testing are protocol and state-machine failures. And the
# day an ESP-IDF project exists, the device links this same library rather than
# growing a second implementation that drifts.
add_library(attadipa_debug
    src/protocol.cpp
    src/bridge.cpp
)
add_library(attadipa::debug ALIAS attadipa_debug)

target_include_directories(attadipa_debug PUBLIC include)
target_link_libraries(attadipa_debug PUBLIC attadipa::core attadipa::link)
