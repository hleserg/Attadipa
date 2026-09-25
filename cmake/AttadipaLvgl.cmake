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
# That tree has to be a git checkout of the pinned commit: a tarball or a copy
# with no git metadata has nothing to verify its identity with, and is refused.
#
# The order is fetch, verify, *then* add (#637). FetchContent_MakeAvailable
# would run LVGL's own CMakeLists.txt as it adds it, so a moved tag or a wrong
# local tree would already have executed before the checks below could refuse
# it. SOURCE_SUBDIR names a directory that does not exist, which the CMake
# documentation (3.18+) gives as the way to make MakeAvailable fetch without
# adding; add_subdirectory at the end of this file adds the verified tree.

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
    FetchContent_Declare(lvgl
        SOURCE_DIR    "${ATTADIPA_LVGL_SOURCE_DIR}"
        SOURCE_SUBDIR attadipa-fetch-only
    )
else()
    message(STATUS "LVGL: cloning ${ATTADIPA_LVGL_TAG}")
    FetchContent_Declare(lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG        ${ATTADIPA_LVGL_TAG}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        SOURCE_SUBDIR  attadipa-fetch-only
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
#    checked this way, and is refused: the version header it carries is part of
#    the content being checked, not evidence about it.
find_package(Git QUIET)
if(NOT Git_FOUND)
    message(FATAL_ERROR
        "git was not found, and the simulator build needs it to verify that LVGL "
        "is commit ${ATTADIPA_LVGL_COMMIT}. Install git and configure again.")
endif()

# git searches upward from -C. Without a ceiling, a tree with no .git of its
# own inside build-sim/_deps answers with the enclosing Attadipa HEAD.
file(REAL_PATH "${lvgl_SOURCE_DIR}/.." _lv_parent)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${_lv_parent}"
            "${GIT_EXECUTABLE}" -C "${lvgl_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _lv_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE _lv_head_error
    RESULT_VARIABLE _lv_head_result)

if(NOT EXISTS "${lvgl_SOURCE_DIR}/.git")
    if(ATTADIPA_LVGL_SOURCE_DIR)
        set(_lv_remedy "Point ATTADIPA_LVGL_SOURCE_DIR at a git clone checked out at that commit.")
    else()
        # FetchContent's stamps outlive the source tree, so deleting only
        # the tree reruns the update step against nothing.
        set(_lv_remedy "Delete ${FETCHCONTENT_BASE_DIR} and configure again to fetch it afresh.")
    endif()
    message(FATAL_ERROR
        "LVGL at ${lvgl_SOURCE_DIR} is not a git checkout, so the commit pin "
        "${ATTADIPA_LVGL_COMMIT} cannot be verified and the tree is refused.\n"
        "${_lv_remedy}")
elseif(NOT _lv_head_result EQUAL 0)
    message(FATAL_ERROR
        "git could not read the commit of LVGL at ${lvgl_SOURCE_DIR}, so the pin "
        "${ATTADIPA_LVGL_COMMIT} cannot be verified and the tree is refused.\n"
        "git said: ${_lv_head_error}")
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

# Only now, verified, does LVGL's own CMake run.
add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}")
