# The local GNSS receiver: a wire format, and the one provider that speaks it.
#
# It sits beside `link/` and for the same reason — `link/` knows what bytes
# 36-43 of RESP_CODE_SELF_INFO mean, this knows what `$GNRMC` means, and
# `core::LocationService` is not allowed to learn either. Both link core rather
# than the other way round, so a service can describe a position without
# depending on the thing that produced it.
#
# It is *not* in `platform/`, which is where issue #429 asked for it. That
# request is stale against the code: `core/CMakeLists.txt:30` — "PRIVATE
# attadipa_platform" — means core already depends on platform, so a class
# deriving `core::PositionProvider` cannot live below it without a cycle. The
# issue's actual requirement — board pins and module choice stay out of core,
# and nothing above the driver learns which receiver answered — is met here:
# the pins are in `firmware/main/`, and what leaves this library is a
# `GnssObservation` like any other.

# minmea is C and this project is `LANGUAGES CXX`. Enabling it here rather than
# at the root keeps the dependency where the dependency is; the simulator's
# LVGL fetch does the same thing for the same reason.
enable_language(C)

# The vendored parser, built as its own target so that the flags it needs stay
# off everything else. See `vendor/minmea/README.attadipa.md` for the pin and
# for what may not be changed about these two files.
add_library(attadipa_minmea STATIC vendor/minmea/minmea.c)

# SYSTEM, and it is load-bearing rather than tidy. `minmea.h` is C: it has an
# anonymous struct and an int-to-float conversion, and the strict warning job
# compiles this project with `-Wpedantic -Wconversion -Werror`, so including it
# as an ordinary header fails the build in a file nobody here may edit. SYSTEM
# is how a vendored header stays byte-identical *and* the project keeps its own
# warnings turned all the way up.
target_include_directories(attadipa_minmea SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/minmea)

# Upstream's own CMakeLists sets exactly these, and they are what make the
# libc declare the functions `minmea.c` calls.
target_compile_definitions(attadipa_minmea PRIVATE
    _POSIX_C_SOURCE=199309L
    _BSD_SOURCE
    _DEFAULT_SOURCE
    _DARWIN_C_SOURCE
)

# `timegm` is not in the toolchain this project ships with: `CONFIG_LIBC_NEWLIB=y`
# on both boards, and the esp32s3 `libc.a` defines `mktime` and no `timegm`
# (`nm --defined-only`). Upstream's README names this substitution as the
# remedy, and it is safe here for a reason that is not "the timezone is
# probably UTC": **Attadipa calls neither `minmea_gettime` nor
# `minmea_getdatetime`.** `nmea_receiver.cpp` converts the date and time itself
# through `core::wall_time_from_civil`. The definition is on this target alone
# so that no other translation unit inherits a redefined `timegm`.
target_compile_definitions(attadipa_minmea PRIVATE timegm=mktime)

# The vendored file is not ours to keep warning-clean, and it must stay
# byte-identical, so the two things that would eventually force an edit are
# turned off for it and for nothing else. `-Wall -Wextra` stay on: upstream
# builds with them and with `-Werror`, so a warning here is news.
target_compile_options(attadipa_minmea PRIVATE -Wno-unused-parameter)

add_library(attadipa_gnss src/nmea_receiver.cpp)
add_library(attadipa::gnss ALIAS attadipa_gnss)

target_include_directories(attadipa_gnss PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# minmea is PRIVATE and that is the boundary: no header under `include/` names
# a minmea type, so nothing that links this library can start parsing sentences
# of its own. `attadipa_core` is PUBLIC because the class this library exports
# *is* a `core::PositionProvider` and a caller has to see that.
target_link_libraries(attadipa_gnss
    PUBLIC  attadipa_core
    PRIVATE attadipa_minmea
)
