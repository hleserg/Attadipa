# Services, and the capability registry that maps hardware onto them.
#
# attadipa_platform is linked PRIVATE, and that keyword is the enforcement
# mechanism for docs/adr/0007-two-capability-layers.md §5: core/ compiles
# against the hardware inventory, and nothing that links core/ inherits the
# ability to. Changing PRIVATE to PUBLIC here silently deletes the boundary,
# which is why tests/boundary/ checks it rather than trusting it.
add_library(attadipa_core
    src/availability.cpp
    src/capability.cpp
    src/capability_registry.cpp
    src/diagnostics.cpp
    src/geo.cpp
    src/gnss_power.cpp
    src/input.cpp
    src/motion.cpp
    src/position.cpp
    src/power_state.cpp
    src/time_service.cpp
    src/trust.cpp
)
add_library(attadipa::core ALIAS attadipa_core)

target_include_directories(attadipa_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(attadipa_core
    PUBLIC  attadipa_headers
    PRIVATE attadipa_platform
)
