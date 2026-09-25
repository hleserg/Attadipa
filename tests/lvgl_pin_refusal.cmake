# cmake/AttadipaLvgl.cmake must refuse an LVGL tree it cannot verify *before*
# that tree's own CMakeLists.txt runs (#637).
#
# Each fixture is a fake LVGL: an lv_version.h that says 9.5.0, which passes
# the version check, and a CMakeLists.txt that writes a sentinel file. The
# sentinel is the proof — if it exists after a refused configure, the tree ran
# before it was refused. The positive control pins the fixture's own HEAD and
# must write the sentinel, or the sentinel proves nothing.
#
# Run as: cmake -DWORK_DIR=<dir> -DREPO_DIR=<repo root> -P lvgl_pin_refusal.cmake

find_package(Git REQUIRED)

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(_driver "${WORK_DIR}/driver")
file(WRITE "${_driver}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.20)\n"
    "project(lvgl_pin_driver NONE)\n"
    "include(\"${REPO_DIR}/cmake/AttadipaLvgl.cmake\")\n")

function(make_fixture dir)
    file(WRITE "${dir}/lv_version.h"
        "#define LVGL_VERSION_MAJOR 9\n"
        "#define LVGL_VERSION_MINOR 5\n"
        "#define LVGL_VERSION_PATCH 0\n")
    file(WRITE "${dir}/CMakeLists.txt"
        "file(WRITE \"${dir}.ran\" \"LVGL's CMake ran\")\n")
endfunction()

function(git_in dir)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} in ${dir} failed: ${err}")
    endif()
    set(git_out "${out}" PARENT_SCOPE)
endfunction()

# configure(<name> <fixture dir> <expect pass?> <regex> [extra -D args...])
function(configure name dir expect_pass regex)
    # A fixture under the repository's build tree would otherwise resolve to
    # the repository's own HEAD.
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${WORK_DIR}"
                "${CMAKE_COMMAND}" -S "${_driver}" -B "${WORK_DIR}/build-${name}"
                "-DATTADIPA_LVGL_SOURCE_DIR=${dir}" ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    # CMake wraps a long message, so a deep build path splits the phrase.
    string(REGEX REPLACE "[ \t\r\n]+" " " log "${out}${err}")
    if(expect_pass AND NOT rc EQUAL 0)
        message(FATAL_ERROR "${name}: configure should pass and failed:\n${log}")
    endif()
    if(NOT expect_pass AND rc EQUAL 0)
        message(FATAL_ERROR "${name}: configure should be refused and passed:\n${log}")
    endif()
    if(NOT log MATCHES "${regex}")
        message(FATAL_ERROR "${name}: expected /${regex}/ in:\n${log}")
    endif()
    if(expect_pass AND NOT EXISTS "${dir}.ran")
        message(FATAL_ERROR "${name}: the verified tree was never added")
    endif()
    if(NOT expect_pass AND EXISTS "${dir}.ran")
        message(FATAL_ERROR "${name}: the tree's CMake ran before it was refused")
    endif()
    message(STATUS "${name}: ok")
endfunction()

# A copy with no git metadata cannot prove what it is.
make_fixture("${WORK_DIR}/nogit")
configure(nogit "${WORK_DIR}/nogit" FALSE "is not a git checkout")

# A git checkout at a commit other than the pin.
set(_git "${WORK_DIR}/wronghead")
make_fixture("${_git}")
git_in("${_git}" init -q)
git_in("${_git}" add -A)
git_in("${_git}" -c user.name=t -c user.email=t@t -c commit.gpgsign=false
       commit -q -m fixture)
configure(wronghead "${_git}" FALSE "LVGL commit mismatch")

# Control: the same tree, pinned at its own HEAD, is verified and added.
git_in("${_git}" rev-parse HEAD)
configure(pinned "${_git}" TRUE "LVGL commit verified"
          "-DATTADIPA_LVGL_COMMIT=${git_out}")
