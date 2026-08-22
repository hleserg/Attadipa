// The two pieces of the widgets that can be wrong quietly.
//
// A gauge and a status row are drawings, and most of what they do is only
// checkable by looking — which is why the visual matrix exists and why the
// battery went back to the bench once already for reading as a toggle switch.
// But two things in them are arithmetic and a rule, and those do not need eyes:
// how much of the box a charge fills, and which availabilities count as lit.
//
// Both are out in the header for exactly this reason. A test that had to stand
// up an LVGL display to discover that 0 % draws as one pixel would be a test
// nobody runs.

#include <cassert>
#include <cstdio>

#include "attadipa/core/availability.h"
#include "attadipa/ui/widgets.h"

using attadipa::core::Availability;
using attadipa::ui::widgets::battery_fill_px;
using attadipa::ui::widgets::is_lit;

namespace {

void the_ends_of_the_scale_are_exact()
{
    // The two a person actually checks. Anything else may be off by a pixel and
    // nobody will ever know; these two may not.
    assert(battery_fill_px(0, 100) == 0);
    assert(battery_fill_px(100, 100) == 100);
    assert(battery_fill_px(0, 47) == 0);
    assert(battery_fill_px(100, 47) == 47);
}

void a_charge_that_is_not_zero_never_draws_as_zero()
{
    // Integer division rounds down, so on the T-Watch's 52-pixel gauge every
    // charge below 2 % would floor to nothing. An empty box is a claim — "flat"
    // — and a person acts on it. A sliver is the truth.
    for (int inner = 1; inner <= 120; ++inner) {
        for (int percent = 1; percent <= 100; ++percent) {
            const auto fill = battery_fill_px(static_cast<std::uint8_t>(percent), inner);
            assert(fill >= 1);
            assert(fill <= inner);
        }
    }
}

void nothing_ever_overflows_the_box()
{
    // A fuel gauge that has just been calibrated reports over 100. The widget
    // clamps rather than drawing a fill wider than the shell that contains it.
    assert(battery_fill_px(101, 40) == 40);
    assert(battery_fill_px(255, 40) == 40);
    // And a degenerate box is not a crash or a negative width.
    assert(battery_fill_px(50, 0) == 0);
    assert(battery_fill_px(50, -3) == 0);
}

void the_fill_is_monotonic()
{
    // More charge is never less bar. Obvious, and exactly the property an
    // "improved" rounding rule breaks first.
    for (int inner : {13, 33, 52, 78, 101}) {
        std::int32_t previous = 0;
        for (int percent = 0; percent <= 100; ++percent) {
            const auto fill = battery_fill_px(static_cast<std::uint8_t>(percent), inner);
            assert(fill >= previous);
            previous = fill;
        }
    }
}

void only_ready_lights_an_icon()
{
    // Six ways of not working, and the row draws all six the same. That is the
    // decision: the face says *whether*, and the reason lives one tap away —
    // the previous design put the reason on the face and it read like a debug
    // line, because a capability name and an availability name joined by a
    // middle dot is one.
    assert(is_lit(Availability::Ready));
    assert(!is_lit(Availability::Unsupported));
    assert(!is_lit(Availability::Unprovisioned));
    assert(!is_lit(Availability::Unreachable));
    assert(!is_lit(Availability::Incompatible));
    assert(!is_lit(Availability::Failed));
    assert(!is_lit(Availability::Off));
}

}  // namespace

int main()
{
    the_ends_of_the_scale_are_exact();
    a_charge_that_is_not_zero_never_draws_as_zero();
    nothing_ever_overflows_the_box();
    the_fill_is_monotonic();
    only_ready_lights_an_icon();
    std::printf("ui widgets: ok\n");
    return 0;
}
