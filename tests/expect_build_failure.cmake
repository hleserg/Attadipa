# Assert that a build target fails, and fails for the stated reason.
#
# CTest's WILL_FAIL turns "the build broke" into a passing test, which is
# exactly right until the build breaks for some other reason — a typo in the
# fixture, a missing library, a renamed target. Then the architecture test goes
# green while proving nothing. This script asks for the failure *and* for the
# compiler's own words about it.
#
#
# Usage:
#   cmake -DBUILD_DIR=... -DTARGET=... -DCONFIG=... -DEXPECT_MATCHES=<regex>
#         [-DEXPLAIN=<what a successful build would mean>]
#         -P expect_build_failure.cmake
#
# EXPLAIN is what the reader is told when the target builds and should not have.
# It is per-call because the callers are checking different rules — a layer
# boundary, a type that must refuse a `bool` — and a message about the wrong one
# sends the next person to the wrong file.

foreach(var BUILD_DIR TARGET EXPECT_MATCHES)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "expect_build_failure.cmake: -D${var} is required")
    endif()
endforeach()

if(NOT DEFINED EXPLAIN)
    set(EXPLAIN "A rule this repository enforces through the compiler is no longer enforced.")
endif()

set(_config_args)
if(CONFIG)
    set(_config_args --config "${CONFIG}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${BUILD_DIR}" --target "${TARGET}" ${_config_args}
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    RESULT_VARIABLE _result)

set(_all "${_out}${_err}")

if(_result EQUAL 0)
    message(FATAL_ERROR
        "Target '${TARGET}' BUILT, and it must not.\n"
        "${EXPLAIN}\n"
        "--- build output ---\n${_all}")
endif()

if(NOT _all MATCHES "${EXPECT_MATCHES}")
    message(FATAL_ERROR
        "Target '${TARGET}' failed, but not for the reason this test is about.\n"
        "  expected the output to match: ${EXPECT_MATCHES}\n"
        "A failure for any other reason makes this test green while proving "
        "nothing, which is worse than not having it.\n"
        "--- build output ---\n${_all}")
endif()

message(STATUS "'${TARGET}' failed as required, and the message says why.")
