// What ended a light sleep, decided by the shipping operation rather than by a
// copy of it: `firmware/main/wake_classification.h` is the header
// `board_power.cpp` calls, and the only thing this file substitutes for is the
// AXP2101 read, which is a callable there for exactly this reason.
//
// The matrix below is #367's P3 finding. The board wakes on a GPIO edge from
// the touch line or on the PMU poll timer, and in both cases the power key is
// found afterwards by reading a write-one-to-clear register -- so the read that
// proves a press is also the read that destroys the proof. The defect was that
// only one of the two routes reported what it had spent: a touch and a press
// arriving together came back as a touch alone, with the evidence already
// cleared. Every row here therefore asserts two things, what was reported and
// whether the latch was touched at all, because a route that reads the register
// and stays silent is the failure and it is invisible in the causes alone.

#include <cstdio>

#include "wake_classification.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

using attadipa::core::WakeCauses;
using attadipa::core::WakeSource;
using attadipa::core::wake_bit;
using attadipa::firmware::classify_wake;
using attadipa::firmware::WakeVerdict;

// The AXP2101 latch, and a count of how often it was asked.
//
// `reads` is half the point of the fixture. The register is write-one-to-clear
// and the production callable clears what it finds, so a second read in one
// wake would return false whatever the hardware did -- and a read on a route
// that then reports nothing is a press deleted in silence.
struct Latch {
    bool edge = false;
    unsigned reads = 0;

    bool consume()
    {
        ++reads;
        const bool found = edge;
        edge = false;
        return found;
    }
};

constexpr std::uint16_t kTouch  = wake_bit(WakeSource::Touch);
constexpr std::uint16_t kTimer  = wake_bit(WakeSource::Timer);
constexpr std::uint16_t kButton = wake_bit(WakeSource::Button);
constexpr std::uint16_t kUsb    = wake_bit(WakeSource::Usb);

WakeVerdict run(WakeCauses& causes, Latch& latch, bool debug_wake = false)
{
    return classify_wake(causes, debug_wake, [&latch] { return latch.consume(); });
}

// The row the finding is about. On `c81d361` this reported Touch alone.
void test_touch_and_a_press_are_both_reported()
{
    WakeCauses causes;
    causes.from_soc = kTouch;
    Latch latch{true, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.derived == kButton);
    CHECK((causes.from_soc | causes.derived) == (kTouch | kButton));
    CHECK(latch.reads == 1);
}

void test_touch_alone_derives_nothing()
{
    WakeCauses causes;
    causes.from_soc = kTouch;
    Latch latch{false, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.derived == 0);
    CHECK(causes.from_soc == kTouch);
    CHECK(latch.reads == 1);
}

// Both SoC bits set at once is the case the bitmap API exists for, and it must
// not become a race between two branches over which one classifies the wake.
void test_touch_and_timer_together_keep_all_three()
{
    WakeCauses causes;
    causes.from_soc = kTouch | kTimer;
    Latch latch{true, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK((causes.from_soc | causes.derived) == (kTouch | kTimer | kButton));
    CHECK(latch.reads == 1);
}

void test_the_poll_finds_the_press()
{
    WakeCauses causes;
    causes.from_soc = kTimer;
    Latch latch{true, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK((causes.from_soc | causes.derived) == (kTimer | kButton));
    CHECK(latch.reads == 1);
}

// The ordinary case, three thousand times per five minutes of sleep: the poll
// fired, nothing was pressed, go back down.
void test_an_empty_poll_goes_back_down()
{
    WakeCauses causes;
    causes.from_soc = kTimer;
    Latch latch{false, 0};

    CHECK(run(causes, latch) == WakeVerdict::PollAgain);
    CHECK(causes.derived == 0);
    CHECK(latch.reads == 1);
}

// A source nobody armed is the state ADR-0016 exists to detect, and the owner
// reports it. Reading the PMU here would spend a latch to answer a question
// that was not asked -- the press would be gone from the next descent too.
void test_an_unarmed_cause_is_reported_without_spending_the_latch()
{
    WakeCauses causes;
    causes.from_soc = kUsb;
    causes.unmapped_from_soc = 0x8000'0000u;
    Latch latch{true, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.derived == 0);
    CHECK(causes.from_soc == kUsb);
    CHECK(latch.reads == 0);
    CHECK(latch.edge);
}

// The same for a wake with no named cause at all: the SoC said something this
// project cannot map, and the raw word is all the evidence there is.
void test_an_unmapped_cause_survives_classification()
{
    WakeCauses causes;
    causes.from_soc = 0;
    causes.unmapped_from_soc = 0x0000'0040u;
    Latch latch{false, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.from_soc == 0);
    CHECK(causes.unmapped_from_soc == 0x0000'0040u);
    CHECK(causes.derived == 0);
    CHECK(latch.reads == 0);
}

// The debug descent sleeps for 750 ms to prove the path, not to poll the PMU.
// It is over when it comes back, and a press latched meanwhile belongs to the
// next real descent rather than to this one.
void test_a_debug_timer_wake_leaves_the_latch_alone()
{
    WakeCauses causes;
    causes.from_soc = kTimer;
    Latch latch{true, 0};

    CHECK(run(causes, latch, true) == WakeVerdict::Report);
    CHECK(causes.derived == 0);
    CHECK(latch.reads == 0);
    CHECK(latch.edge);
}

// But a touch during that same descent is a real wake on an armed source, and
// the press that came with it is classified like any other.
void test_a_debug_descent_that_ends_on_a_touch_still_reports_the_press()
{
    WakeCauses causes;
    causes.from_soc = kTouch;
    Latch latch{true, 0};

    CHECK(run(causes, latch, true) == WakeVerdict::Report);
    CHECK((causes.from_soc | causes.derived) == (kTouch | kButton));
    CHECK(latch.reads == 1);
}

// A failed I2C read is indistinguishable from an empty register here, by
// design: the production callable logs the failure and returns false. What
// matters is that it is not upgraded into a Button nobody proved.
void test_a_failed_read_derives_nothing()
{
    WakeCauses causes;
    causes.from_soc = kTouch | kTimer;
    Latch latch{false, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.derived == 0);
    CHECK(latch.reads == 1);
}

// Classification adds to `derived` and never edits what the silicon said. The
// two halves are separate so that only `from_soc` can be reconciled against
// what was armed (power_owner.h), and a board that folded Button into it would
// make every press read as an unreconciled wake source.
void test_the_socs_own_word_is_never_edited()
{
    WakeCauses causes;
    causes.from_soc = kTouch | kTimer | kUsb;
    causes.derived = 0;
    causes.unmapped_from_soc = 0x1234'5678u;
    Latch latch{true, 0};

    CHECK(run(causes, latch) == WakeVerdict::Report);
    CHECK(causes.from_soc == (kTouch | kTimer | kUsb));
    CHECK(causes.unmapped_from_soc == 0x1234'5678u);
    CHECK(causes.derived == kButton);
}

}  // namespace

int main()
{
    test_touch_and_a_press_are_both_reported();
    test_touch_alone_derives_nothing();
    test_touch_and_timer_together_keep_all_three();
    test_the_poll_finds_the_press();
    test_an_empty_poll_goes_back_down();
    test_an_unarmed_cause_is_reported_without_spending_the_latch();
    test_an_unmapped_cause_survives_classification();
    test_a_debug_timer_wake_leaves_the_latch_alone();
    test_a_debug_descent_that_ends_on_a_touch_still_reports_the_press();
    test_a_failed_read_derives_nothing();
    test_the_socs_own_word_is_never_edited();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("wake_classification: OK");
    return 0;
}
