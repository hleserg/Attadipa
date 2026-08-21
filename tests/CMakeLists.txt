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
# Localization (docs/adr/0010-localization.md).

add_executable(test_l10n test_l10n.cpp)
target_link_libraries(test_l10n PRIVATE firefly_l10n)
add_test(NAME l10n COMMAND test_l10n)

# The other boundary, and it points the opposite way. An application must not
# learn which chip answered; the core must not learn which language will be
# read. Same mechanism, same pair of tests: the fixture builds against apps and
# must not build against core.
add_executable(l10n_boundary_positive boundary/core_reaches_for_words.cpp)
target_link_libraries(l10n_boundary_positive PRIVATE firefly_apps)
add_test(NAME boundary_apps_can_speak COMMAND l10n_boundary_positive)

add_executable(l10n_boundary_negative EXCLUDE_FROM_ALL boundary/core_reaches_for_words.cpp)
target_link_libraries(l10n_boundary_negative PRIVATE firefly_core)
add_test(NAME boundary_core_cannot_speak
         COMMAND ${CMAKE_COMMAND}
                 -DBUILD_DIR=${CMAKE_BINARY_DIR}
                 -DTARGET=l10n_boundary_negative
                 -DCONFIG=$<CONFIG>
                 "-DEXPECT_MATCHES=tr\\.h.*(No such file|file not found|Cannot open)"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/expect_build_failure.cmake)
set_tests_properties(boundary_core_cannot_speak PROPERTIES LABELS architecture)

# ---------------------------------------------------------------------------
# The three checks ADR-0010 §3 asks a machine to make, plus the check that the
# checks work.
#
# They are ctest entries rather than CI-only steps so that a local run and CI
# enforce exactly the same thing, and neither needs its own copy of the rule.
#
# If Python 3.11 is not on the machine, these are registered as FAILING rather
# than skipped. "The check did not run" must never read as "the check passed" —
# the same reason a hardware test in this repository says NOT EXECUTED instead
# of PASS. tomllib is why the floor is 3.11.
find_package(Python3 3.11 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME l10n_generated_is_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/l10n/gen_strings.py --check)
    add_test(NAME l10n_glyphs_in_font_subset
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/l10n/check_glyphs.py)
    add_test(NAME l10n_checks_reject_mistakes
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/l10n/selftest.py)
    set_tests_properties(l10n_generated_is_current l10n_glyphs_in_font_subset
                         l10n_checks_reject_mistakes
                         PROPERTIES LABELS localization)
else()
    add_test(NAME l10n_checks_unavailable
             COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(l10n_checks_unavailable PROPERTIES
        LABELS localization)
    message(WARNING
        "Python 3.11+ was not found, so the three localization checks "
        "(ADR-0010 section 3) cannot run. A deliberately failing test is "
        "registered in their place — install Python 3.11 or newer.")
endif()

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

# ---------------------------------------------------------------------------
# GNSS trust (docs/adr/0011-gnss-integrity.md).
#
# Sequences rather than single calls, on purpose. The detectors that matter are
# rate detectors, and a rate needs two epochs — a suite of one-observation tests
# passes cheerfully while every one of them is switched off, which is exactly
# how the interval bug survived being written.

add_executable(test_trust test_trust.cpp)
target_link_libraries(test_trust PRIVATE firefly_core)
add_test(NAME trust COMMAND test_trust)
set_tests_properties(trust PROPERTIES LABELS gnss)
