# Toolchain smoke test. It proves the host build and the CI path work — nothing
# more. It is not evidence about firmware, and certainly not about hardware.
add_executable(test_build_smoke test_build_smoke.cpp)
target_link_libraries(test_build_smoke PRIVATE firefly_headers)
add_test(NAME build_smoke COMMAND test_build_smoke)
