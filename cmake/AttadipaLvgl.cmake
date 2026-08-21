# LVGL, pinned.
#
# docs/research/DEPENDENCIES.md pins v9.5.0 at commit
# 85aa60d18b3d5e5588d7b247abf90198f07c8a63 — the commit whose message is
# "chore: release v9.5.0 (#9753)", and the SHA that GitHub's refs/tags/v9.5.0
# resolved to when it was checked.
#
# The clone asks for the *tag* and the *commit* is verified afterwards.
#
# The tag is not there for speed, and it would be dishonest to imply it is.
# CMake 3.28's generated lvgl-populate-gitclone.cmake runs, verbatim:
#
#     clone --no-checkout --depth 1 --no-single-branch --progress \
#           --config "advice.detachedHead=false" ".../lvgl.git" "lvgl-src"
#     checkout "v9.5.0" --
#
# `--no-single-branch` means GIT_SHALLOW gets one commit off *every* ref, not
# off one branch. Do not read `GIT_SHALLOW TRUE` on the next line as "a small
# download". MEASURED on a GitHub runner, first run with a cold cache
# (run 32462413273): the clone took 22.8 s and the `_deps` tree it left behind
# cached at 366 761 925 B — 350 MiB. On a slow link it is worse than that
# sounds; ATTADIPA_LVGL_SOURCE_DIR below is how a developer avoids it entirely.
#
# The tag is there because `git checkout` of a *tag ref* is what that generated
# script can reliably resolve in such a clone; a bare SHA is fetchable by hand
# (`git fetch --depth 1 origin <sha>` does work against GitHub) but is not what
# FetchContent emits. So: the tag is the transport, and the SHA below is the
# pin. A tag can be moved and a commit cannot, which is why the transport is
# never trusted and the commit is checked after the fact.
#
# CI pays that download once and then caches `_deps`, keyed on this file — see
# .github/workflows/ci.yml. Download cost is a CI concern that belongs in YAML,
# not a reason to invent a second fetch mechanism in here. Editing this file
# invalidates that cache by design: the pin lives here, so a changed pin must
# not be served a tree fetched under the old one.
#
# ATTADIPA_LVGL_SOURCE_DIR points the build at a tree that is already on disk,
# for offline work. It skips the fetch — not the checks, which are the point.

include(FetchContent)

set(ATTADIPA_LVGL_TAG "v9.5.0"
    CACHE STRING "LVGL tag to clone. Verified against ATTADIPA_LVGL_COMMIT after cloning.")
set(ATTADIPA_LVGL_COMMIT "85aa60d18b3d5e5588d7b247abf90198f07c8a63"
    CACHE STRING "The commit that tag must resolve to. Changing it needs a DEPENDENCIES.md entry.")
set(ATTADIPA_LVGL_SOURCE_DIR ""
    CACHE PATH "An LVGL source tree already on disk. Skips the fetch, not the checks.")

set(ATTADIPA_LVGL_EXPECTED_VERSION "9.5.0")

# LVGL's own options, set before it is added so its cache entries take these
# values rather than its defaults.
#
# Demos and examples are off because they are tens of thousands of lines this
# project never calls, and every one of them is a compile-time cost on every
# build. ThorVG is off because nothing here draws SVG or Lottie yet; turning it
# on is a decision with a flash cost, and it should be made deliberately with a
# measurement rather than inherited from a default.
set(CONFIG_LV_BUILD_DEMOS        OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "${CMAKE_SOURCE_DIR}/sim/lv_conf_simulator.h" CACHE PATH "" FORCE)

# LVGL is C, and its CMake enables CXX and ASM for itself. C has to be on
# before it is added, because as a subproject it never calls project().
enable_language(C)

if(ATTADIPA_LVGL_SOURCE_DIR)
    message(STATUS "LVGL: using the tree at ${ATTADIPA_LVGL_SOURCE_DIR} (no fetch)")
    FetchContent_Declare(lvgl SOURCE_DIR "${ATTADIPA_LVGL_SOURCE_DIR}")
else()
    message(STATUS "LVGL: cloning ${ATTADIPA_LVGL_TAG}")
    FetchContent_Declare(lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG        ${ATTADIPA_LVGL_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
endif()

FetchContent_MakeAvailable(lvgl)

# Never trust, verify — the rule applies to dependencies too. A moved tag, a
# stale ATTADIPA_LVGL_SOURCE_DIR or a half-updated FetchContent cache all produce
# a build that compiles against a version nobody chose, and that failure
# surfaces much later as behaviour rather than as an error. It costs two checks
# to refuse instead.

# 1. The version the source claims.
file(READ "${lvgl_SOURCE_DIR}/lv_version.h" _attadipa_lv_version_h)
string(REGEX MATCH "#define LVGL_VERSION_MAJOR ([0-9]+)" _m "${_attadipa_lv_version_h}")
set(_lv_major "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define LVGL_VERSION_MINOR ([0-9]+)" _m "${_attadipa_lv_version_h}")
set(_lv_minor "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define LVGL_VERSION_PATCH ([0-9]+)" _m "${_attadipa_lv_version_h}")
set(_lv_patch "${CMAKE_MATCH_1}")
set(ATTADIPA_LVGL_ACTUAL_VERSION "${_lv_major}.${_lv_minor}.${_lv_patch}")

# A version that could not be parsed is not a version. Upstream has already
# moved this header once — on LVGL master, lv_version.h is a deprecation shim
# that includes include/lvgl/lv_version.h — so "the regex found nothing" is a
# real case and not a hypothetical, and it must read as a refusal rather than
# as a mismatch against an empty string.
if(_lv_major STREQUAL "" OR _lv_minor STREQUAL "" OR _lv_patch STREQUAL "")
    message(FATAL_ERROR
        "Could not read a version out of ${lvgl_SOURCE_DIR}/lv_version.h.\n"
        "Either that is not an LVGL ${ATTADIPA_LVGL_EXPECTED_VERSION} tree, or the "
        "header has moved upstream and this check needs updating along with the pin.")
endif()

if(NOT ATTADIPA_LVGL_ACTUAL_VERSION STREQUAL ATTADIPA_LVGL_EXPECTED_VERSION)
    message(FATAL_ERROR
        "LVGL version mismatch.\n"
        "  expected: ${ATTADIPA_LVGL_EXPECTED_VERSION} (docs/research/DEPENDENCIES.md)\n"
        "  found:    ${ATTADIPA_LVGL_ACTUAL_VERSION} in ${lvgl_SOURCE_DIR}\n"
        "Bumping LVGL is a dependency decision: update DEPENDENCIES.md and "
        "ATTADIPA_LVGL_EXPECTED_VERSION together, and retest both geometries.")
endif()

# 2. The commit the source actually is. This is the check that catches a moved
#    tag, which the version header cannot: a re-tagged v9.5.0 would still say
#    9.5.0. A tree with no git metadata — a tarball, a vendored copy — cannot be
#    checked this way, and says so rather than passing quietly.
find_package(Git QUIET)
set(_lv_head "")
if(Git_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${lvgl_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _lv_head
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _lv_head_result)
    if(NOT _lv_head_result EQUAL 0)
        set(_lv_head "")
    endif()
endif()

if(_lv_head STREQUAL "")
    message(WARNING
        "LVGL ${ATTADIPA_LVGL_ACTUAL_VERSION} at ${lvgl_SOURCE_DIR} is not a git checkout, "
        "so the commit pin ${ATTADIPA_LVGL_COMMIT} could not be verified. "
        "The version header was checked and agrees.")
elseif(NOT _lv_head STREQUAL ATTADIPA_LVGL_COMMIT)
    message(FATAL_ERROR
        "LVGL commit mismatch — the tag does not point where it did.\n"
        "  expected: ${ATTADIPA_LVGL_COMMIT} (docs/research/DEPENDENCIES.md)\n"
        "  found:    ${_lv_head}\n"
        "  tag:      ${ATTADIPA_LVGL_TAG} at ${lvgl_SOURCE_DIR}\n"
        "This is the case the version header cannot catch: a re-tagged release "
        "still says ${ATTADIPA_LVGL_ACTUAL_VERSION}. Find out what moved before "
        "changing the pin.")
else()
    message(STATUS "LVGL commit verified: ${_lv_head}")
endif()

message(STATUS "LVGL ${ATTADIPA_LVGL_ACTUAL_VERSION} at ${lvgl_SOURCE_DIR}")
