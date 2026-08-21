# The transport layer beneath the node protocol: framing, queues and the one
# state machine that owns "is the link up".
#
# It sits *below* core in the architecture — core is services, this is plumbing
# — so it links attadipa_core rather than the other way round, and the vocabulary
# it reports in (TransportPhase, DisconnectReason) is core's, not its own. That
# direction is what stops a diagnostics snapshot from depending on a transport
# implementation to describe itself.
#
# Nothing here knows what a BLE stack, a USB peripheral or a UART is. That is
# not abstraction for its own sake: the owner's requirement is that Attadipa's
# node link is not nailed to BLE, and MeshCore's own #3049 is the same
# realisation arriving upstream after four first-party clients had been built.
add_library(attadipa_link
    src/frame_codec.cpp
    src/link_state.cpp
)
add_library(attadipa::link ALIAS attadipa_link)

target_include_directories(attadipa_link PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_link PUBLIC attadipa_core)
