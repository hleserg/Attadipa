# Host tests. Nothing here runs on hardware, and nothing here may be reported as
# if it had — CLAUDE.md's "mock is not hardware" rule. A green run means the
# rules in the ADRs are implemented as written; it says nothing about a board.

# Toolchain smoke test. It proves the host build and the CI path work — nothing
# more. It is not evidence about firmware, and certainly not about hardware.
add_executable(test_build_smoke test_build_smoke.cpp)
target_link_libraries(test_build_smoke PRIVATE attadipa_headers)
add_test(NAME build_smoke COMMAND test_build_smoke)

# The two capability layers, and the ADR rules that live on top of them.
add_executable(test_capability_registry test_capability_registry.cpp)
target_link_libraries(test_capability_registry PRIVATE
    attadipa_platform
    attadipa_core
    attadipa_apps
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
# The same source file is compiled twice. Against attadipa_platform it must
# build; against attadipa_apps it must not. Two tests rather than one, because a
# single failing compile cannot distinguish an enforced boundary from a typo in
# an #include path.

add_executable(boundary_positive boundary/app_reaches_for_hardware.cpp)
target_link_libraries(boundary_positive PRIVATE attadipa_platform)
add_test(NAME boundary_platform_can_see_platform COMMAND boundary_positive)

add_executable(boundary_negative EXCLUDE_FROM_ALL boundary/app_reaches_for_hardware.cpp)
target_link_libraries(boundary_negative PRIVATE attadipa_apps)
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
target_link_libraries(test_l10n PRIVATE attadipa_l10n)
add_test(NAME l10n COMMAND test_l10n)

# The other boundary, and it points the opposite way. An application must not
# learn which chip answered; the core must not learn which language will be
# read. Same mechanism, same pair of tests: the fixture builds against apps and
# must not build against core.
add_executable(l10n_boundary_positive boundary/core_reaches_for_words.cpp)
target_link_libraries(l10n_boundary_positive PRIVATE attadipa_apps)
add_test(NAME boundary_apps_can_speak COMMAND l10n_boundary_positive)

add_executable(l10n_boundary_negative EXCLUDE_FROM_ALL boundary/core_reaches_for_words.cpp)
target_link_libraries(l10n_boundary_negative PRIVATE attadipa_core)
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

    # The design-system boundary, same family as the two compile boundaries
    # above and for the same reason: T-009's acceptance criterion is that no raw
    # colour, pixel count or duration appears in screen code, and that is a
    # property of the source tree rather than of any one screen. Its self-test
    # is registered beside it because a checker nobody has ever seen fail is a
    # checker nobody has tested.
    # The generated fonts are committed, so something has to notice when their
    # inputs change. `--check` with no Node and no configured build compares a
    # digest of charset.py, the size list and the source TTF's hash — which
    # catches the case that actually happens: somebody adds a codepoint and
    # forgets that four .c files were built from the old list.
    add_test(NAME ui_fonts_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/font/generate_ui_fonts.py --check)
    add_test(NAME ui_no_raw_values
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/ui/check_raw_values.py)
    add_test(NAME ui_check_rejects_mistakes
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/ui/selftest.py)
    # The image pipeline, same shape as the fonts: the generated tree is
    # committed, so something has to notice when its inputs change. The digest
    # covers the converter as well as the art, because an encoder that changes
    # its output *is* the asset changing. Its self-test builds each mistake the
    # pipeline refuses — a 1440-pixel concept sheet, a source under docs/, a
    # size with no drawing behind it — and fails if the tool accepted one.
    add_test(NAME ui_images_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/assets/generate_images.py --check)
    set_tests_properties(ui_no_raw_values ui_check_rejects_mistakes
                         ui_fonts_are_current ui_images_are_current
                         PROPERTIES LABELS design)

    # The 16 MB flash addressing ceiling
    # (docs/research/FLASH_ADDRESSING_LIMITS.md).
    #
    # `ota_1` on the Waveshare sits at exactly 0x1000000 and the vendor's own
    # bootloader reads 0x0 when it asks for it — measured, on the unit. What a
    # *running* application does up there was never measured, and of the four
    # runtime paths exactly one (esp_partition_mmap, and only since ESP-IDF
    # v5.5.5) refuses rather than aliasing. So no partition of ours goes at or
    # above that line until somebody measures it.
    #
    # Registered here rather than left to review for the same reason as the
    # boundary compiles above: a rule nobody executes is a rule until the first
    # hurried afternoon. It currently checks zero tables, because there is no
    # ESP-IDF project in this repository yet (issue #127) — which is why the
    # self-test beside it is not optional. It is the only thing proving the
    # checker works at all, and it runs the vendor's own shipped table through
    # it and demands the refusal.
    add_test(NAME flash_partitions_below_ceiling
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/flash/partition_check.py)
    add_test(NAME flash_partition_check_rejects_mistakes
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/flash/selftest.py)
    set_tests_properties(flash_partitions_below_ceiling
                         flash_partition_check_rejects_mistakes
                         PROPERTIES LABELS flash)

    # The other two asset checks have to *draw* something to know whether the
    # drawing changed, so they need Pillow — which the digest check above
    # deliberately does not, so that the primary staleness gate never depends on
    # a package being installed.
    #
    # Absent Pillow, a failing test is registered rather than nothing. A skipped
    # check reads as a passing one in a summary, and the whole point of a
    # self-test is that somebody notices when it stops running.
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import PIL"
                    RESULT_VARIABLE _attadipa_pillow
                    OUTPUT_QUIET ERROR_QUIET)
    if(_attadipa_pillow EQUAL 0)
        add_test(NAME ui_icon_drawings_are_current
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_SOURCE_DIR}/tools/assets/draw_icons.py --check)
        add_test(NAME ui_image_pipeline_rejects_mistakes
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_SOURCE_DIR}/tools/assets/selftest.py)
        set_tests_properties(ui_icon_drawings_are_current
                             ui_image_pipeline_rejects_mistakes
                             PROPERTIES LABELS design)
    else()
        add_test(NAME ui_image_checks_unavailable
                 COMMAND ${CMAKE_COMMAND} -E false)
        set_tests_properties(ui_image_checks_unavailable PROPERTIES LABELS design)
        message(WARNING
            "Pillow was not found, so the asset pipeline's self-test and its "
            "drawing check cannot run. A deliberately failing test is registered "
            "in their place — install it with `apt install python3-pil` or "
            "`pip install pillow`. The digest check ui_images_are_current does "
            "not need it and still runs.")
    endif()
else()
    add_test(NAME l10n_checks_unavailable
             COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(l10n_checks_unavailable PROPERTIES
        LABELS localization)
    add_test(NAME ui_checks_unavailable
             COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(ui_checks_unavailable PROPERTIES LABELS design)
    add_test(NAME flash_checks_unavailable
             COMMAND ${CMAKE_COMMAND} -E false)
    set_tests_properties(flash_checks_unavailable PROPERTIES LABELS flash)
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
if(ATTADIPA_BUILD_SIMULATOR)
    set(_attadipa_shot_dir ${CMAKE_BINARY_DIR}/screenshots)
    file(MAKE_DIRECTORY ${_attadipa_shot_dir})

    foreach(_board t-watch-s3-plus waveshare-amoled-206)
        add_test(NAME simulator_renders_${_board}
                 COMMAND attadipa_sim --board ${_board} --frames 3
                         --screenshot ${_attadipa_shot_dir}/${_board}.png)
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

    # ---------------------------------------------------------------------
    # The generated image assets (T-034).
    #
    # Here rather than with the host tests because an `lv_image_dsc_t` needs
    # LVGL's headers, the same reason `attadipa_images` itself only exists in a
    # simulator build. What it checks is the half Python cannot see: that the
    # bytes which were linked describe themselves correctly and are a drawing
    # rather than a blank rectangle.
    add_executable(test_ui_icons test_ui_icons.cpp)
    target_link_libraries(test_ui_icons PRIVATE attadipa_images)
    add_test(NAME ui_icons COMMAND test_ui_icons)
    set_tests_properties(ui_icons PROPERTIES LABELS design)
endif()

# ---------------------------------------------------------------------------
# GNSS trust (docs/adr/0011-gnss-integrity.md).
#
# Sequences rather than single calls, on purpose. The detectors that matter are
# rate detectors, and a rate needs two epochs — a suite of one-observation tests
# passes cheerfully while every one of them is switched off, which is exactly
# how the interval bug survived being written.

add_executable(test_trust test_trust.cpp)
target_link_libraries(test_trust PRIVATE attadipa_core)
add_test(NAME trust COMMAND test_trust)
set_tests_properties(trust PROPERTIES LABELS gnss)

# ---------------------------------------------------------------------------
# Framing, the bounded queue, and the session state machine.
#
# The test names are the owner's §6 list one for one — fragmented input, partial
# writes, a full queue, a disconnect mid-frame, a reconnect, a large payload, a
# malformed frame — so that coverage against the brief can be read rather than
# asserted. The second half holds our implementation against the defects
# docs/upstream/meshcore-1.17-review.md verified in the upstream serial
# transport, which are bugs in code shipping on these boards today.

add_executable(test_link test_link.cpp)
target_link_libraries(test_link PRIVATE attadipa_link)
add_test(NAME link COMMAND test_link)
set_tests_properties(link PROPERTIES LABELS transport)

# ---------------------------------------------------------------------------
# Device power states and the GNSS receiver's own.
#
# Both tables are exhaustive over every (from, to) pair, because a suite that
# only walks the legal paths passes against a transition_is_legal that returns
# true for everything. The illegal half is where the information is — including
# the rule the MeshCore review found broken upstream: a wake source that only
# exists while the radio is powered must not be armed in a state where it is not.

add_executable(test_power test_power.cpp)
target_link_libraries(test_power PRIVATE attadipa_core)
add_test(NAME power COMMAND test_power)
set_tests_properties(power PROPERTIES LABELS power)

# ---------------------------------------------------------------------------
# The position observation, the validity classifier and integer distance.
#
# The distance function is integer arithmetic over a cosine table, so its error
# is bounded and knowable rather than "floating point, probably fine". The
# tolerances in this file are stated as percentages of a hand-computed answer;
# where the method itself breaks down the test says so rather than widening the
# tolerance until it passes.

add_executable(test_position test_position.cpp)
target_link_libraries(test_position PRIVATE attadipa_core)
add_test(NAME position COMMAND test_position)
set_tests_properties(position PROPERTIES LABELS gnss)

# ---------------------------------------------------------------------------
# The replayable navigation rig (docs/adr/0011-gnss-integrity.md §7).
#
# The interesting GNSS failures cannot be staged. Nobody can arrange to be
# spoofed on a Tuesday afternoon, so a detector for an event that cannot be
# produced on demand is one that gets written once and never verified again.
# The rig replays timestamped observations into the same pure code the device
# runs and asserts the verdict at every step rather than only at the end.
#
# Two targets, and the second is the one that matters. `replay` runs the
# scenarios; `test_replay_rig` proves the runner can fail — a rig that passes
# everything is indistinguishable from a rig that checks nothing.

add_library(attadipa_replay STATIC replay/replay.cpp)
target_include_directories(attadipa_replay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(attadipa_replay PUBLIC attadipa_core)

add_executable(replay replay/replay_main.cpp)
target_link_libraries(replay PRIVATE attadipa_replay)
target_compile_definitions(replay PRIVATE
    ATTADIPA_REPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replay/scenarios")

file(GLOB ATTADIPA_REPLAY_SCENARIOS
     "${CMAKE_CURRENT_SOURCE_DIR}/replay/scenarios/[0-9][0-9]-*.trace")
# 13 is the deliberately-wrong fixture and belongs to the rig's own test.
list(FILTER ATTADIPA_REPLAY_SCENARIOS EXCLUDE REGEX "13-a-fixture-can-fail")
list(LENGTH ATTADIPA_REPLAY_SCENARIOS ATTADIPA_REPLAY_COUNT)
if(ATTADIPA_REPLAY_COUNT LESS 10)
    # A glob that silently matched nothing would produce a test that passes by
    # running no scenarios at all.
    message(FATAL_ERROR "only ${ATTADIPA_REPLAY_COUNT} replay scenarios found")
endif()

add_test(NAME replay COMMAND replay)
set_tests_properties(replay PROPERTIES LABELS gnss)

add_executable(test_replay_rig test_replay_rig.cpp)
target_link_libraries(test_replay_rig PRIVATE attadipa_replay)
target_compile_definitions(test_replay_rig PRIVATE
    ATTADIPA_REPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/replay/scenarios")
add_test(NAME replay_rig COMMAND test_replay_rig)
set_tests_properties(replay_rig PROPERTIES LABELS gnss)

# ---------------------------------------------------------------------------
# The diagnostics snapshot.
#
# Mostly a plain structure, so the tests are about its structural properties
# rather than its behaviour: it must survive a memcpy from a crash handler that
# has no allocator, it must stay small enough to keep in RTC memory, it must
# carry no format — §14, do not tie core to JSON — and every number nobody has
# measured must say so.

add_executable(test_diagnostics test_diagnostics.cpp)
target_link_libraries(test_diagnostics PRIVATE attadipa_core)
add_test(NAME diagnostics COMMAND test_diagnostics)

# ---------------------------------------------------------------------------
# The design tokens (T-009, final §54).
#
# Every value in the token tables is *proposed* — none has been shown on a
# panel — so these tests deliberately pin properties rather than numbers: that
# one token is one physical size on two panels whose densities differ by 21 %,
# that no spacing rounds away to nothing, that a night theme with a hole in it
# does not paint a bright card on a dark page. The three numbers they do pin
# are contrast ratios, which are arithmetic and not taste.

add_executable(test_ui_tokens test_ui_tokens.cpp)
target_link_libraries(test_ui_tokens PRIVATE attadipa_ui)
add_test(NAME ui_tokens COMMAND test_ui_tokens)
set_tests_properties(ui_tokens PROPERTIES LABELS design)
