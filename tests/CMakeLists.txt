# Host tests. Nothing here runs on hardware, and nothing here may be reported as
# if it had — CLAUDE.md's "mock is not hardware" rule. A green run means the
# rules in the ADRs are implemented as written; it says nothing about a board.

# Toolchain smoke test. It proves the host build and the CI path work — nothing
# more. It is not evidence about firmware, and certainly not about hardware.
add_executable(test_build_smoke test_build_smoke.cpp)
target_link_libraries(test_build_smoke PRIVATE firefly_headers)
add_test(NAME build_smoke COMMAND test_build_smoke)

# The two capability layers, and the ADR rules that live on top of them.
add_executable(test_capability_registry test_capability_registry.cpp)
target_link_libraries(test_capability_registry PRIVATE
    firefly_platform
    firefly_core
    firefly_apps
)
add_test(NAME capability_registry COMMAND test_capability_registry)

# ---------------------------------------------------------------------------
# The layer boundary, checked rather than trusted.
#
# docs/adr/0007-two-capability-layers.md §5 says an application that tries to
# ask about a chip fails to build. That is a claim about this build system, and
# a claim about a build system is worth exactly as much as the test that breaks
# when somebody changes PRIVATE to PUBLIC in core/CMakeLists.txt.
#
# The same source file is compiled twice. Against firefly_platform it must
# build; against firefly_apps it must not. Two tests rather than one, because a
# single failing compile cannot distinguish an enforced boundary from a typo in
# an #include path.

add_executable(boundary_positive boundary/app_reaches_for_hardware.cpp)
target_link_libraries(boundary_positive PRIVATE firefly_platform)
add_test(NAME boundary_platform_can_see_platform COMMAND boundary_positive)

add_executable(boundary_negative EXCLUDE_FROM_ALL boundary/app_reaches_for_hardware.cpp)
target_link_libraries(boundary_negative PRIVATE firefly_apps)
# Not WILL_FAIL. WILL_FAIL passes on *any* non-zero exit, so a fixture typo or
# a renamed target would turn this into a test that proves nothing while
# reporting success. The script demands the failure and the compiler's reason
# for it — see tests/expect_build_failure.cmake.
add_test(NAME boundary_apps_cannot_see_platform
         COMMAND ${CMAKE_COMMAND}
                 -DBUILD_DIR=${CMAKE_BINARY_DIR}
                 -DTARGET=boundary_negative
                 -DCONFIG=$<CONFIG>
                 "-DEXPECT_MATCHES=hardware_feature\\.h.*(No such file|file not found|Cannot open)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_build_failure.cmake)
set_tests_properties(boundary_apps_cannot_see_platform PROPERTIES
    LABELS architecture)

# ---------------------------------------------------------------------------
# The simulator, headless.
#
# Only when it is built. A run that renders a frame and writes a screenshot is
# worth more than one that merely fails to crash: the PNG is the artefact a
# design review at both geometries needs, and an empty or missing file is a
# failure the test can see.
if(FIREFLY_BUILD_SIMULATOR)
    set(_firefly_shot_dir ${CMAKE_BINARY_DIR}/screenshots)
    file(MAKE_DIRECTORY ${_firefly_shot_dir})

    foreach(_board t-watch-s3-plus waveshare-amoled-206)
        add_test(NAME simulator_renders_${_board}
                 COMMAND firefly_sim --board ${_board} --frames 3
                         --screenshot ${_firefly_shot_dir}/${_board}.png)
        # SDL needs a video driver it can open with no display attached. This
        # is the "headless run" T-008 asks for rather than the "explicit,
        # stated skip" it allows instead.
        set_tests_properties(simulator_renders_${_board} PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            LABELS simulator)
    endforeach()

    # Both geometries out of one binary, which is T-008's actual acceptance
    # criterion. Nothing was rebuilt between the two tests above; if that ever
    # stops being true, they will still pass, so the criterion is also written
    # down where a reader will find it: docs/architecture/ARCHITECTURE.md.
endif()
