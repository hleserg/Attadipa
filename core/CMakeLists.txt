# Services, and the capability registry that maps hardware onto them.
#
# firefly_platform is linked PRIVATE, and that keyword is the enforcement
# mechanism for docs/adr/0007-two-capability-layers.md §5: core/ compiles
# against the hardware inventory, and nothing that links core/ inherits the
# ability to. Changing PRIVATE to PUBLIC here silently deletes the boundary,
# which is why tests/boundary/ checks it rather than trusting it.
add_library(firefly_core
    src/availability.cpp
    src/capability.cpp
    src/capability_registry.cpp
    src/diagnostics.cpp
    src/geo.cpp
    src/gnss_power.cpp
    src/position.cpp
    src/power_state.cpp
    src/trust.cpp
)
add_library(firefly::core ALIAS firefly_core)

target_include_directories(firefly_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(firefly_core
    PUBLIC  firefly_headers
    PRIVATE firefly_platform
)
