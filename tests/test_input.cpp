#include <cstdio>

#include "attadipa/core/input.h"
#include "power_button_edges.h"

// Host tests for the input layer.
//
// The interesting half is the refusals. A queue that accepts everything and a
// state machine that tolerates nonsense both pass a suite of legal sequences
// cheerfully, and then a swipe that lost its `down` arrives at the interface as
// a drag from wherever the last finger happened to lift. So every test below
// that asserts something is *accepted* has a partner asserting the neighbouring
// impossible thing is *refused* — a second finger, a repeated press, a move
// with nothing down, a release of a button that was never held.
//
// The other half is the cleanup. `release_all` exists for one situation: a
// debug connection drops while it is holding something. It has to lift exactly
// what that origin held, leave a person's finger alone, and lift the pointer
// where it actually is rather than at the origin of the screen — a release at
// (0, 0) would land on whatever sits in the corner and can fire it, which makes
// the cleanup an input event with consequences.

using namespace attadipa::core;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

InputEvent button(InputEventType type, std::uint8_t index, InputOrigin origin = InputOrigin::Remote,
                  std::uint32_t at = 0)
{
    InputEvent e;
    e.type   = type;
    e.button = index;
    e.origin = origin;
    e.at_ms  = at;
    return e;
}

InputEvent pointer(InputEventType type, std::int16_t x, std::int16_t y,
                   InputOrigin origin = InputOrigin::Remote, std::uint8_t touch_id = 0,
                   std::uint32_t at = 0)
{
    InputEvent e;
    e.type     = type;
    e.x        = x;
    e.y        = y;
    e.origin   = origin;
    e.touch_id = touch_id;
    e.at_ms    = at;
    return e;
}

// --- the queue ------------------------------------------------------------

void queue_is_first_in_first_out()
{
    InputQueue q;
    for (std::int16_t i = 0; i < 5; ++i) {
        CHECK(q.push(pointer(InputEventType::PointerMove, i, i)));
    }
    CHECK(q.size() == 5);

    for (std::int16_t i = 0; i < 5; ++i) {
        InputEvent out;
        CHECK(q.pop(out));
        CHECK(out.x == i);
    }
    CHECK(q.empty());

    InputEvent nothing;
    CHECK(!q.pop(nothing));
}

void queue_counts_an_overrun_instead_of_hiding_it()
{
    InputQueue q;
    for (std::size_t i = 0; i < InputQueue::kCapacity; ++i) {
        CHECK(q.push(pointer(InputEventType::PointerMove, 1, 1)));
    }
    // The 65th is refused, and the refusal is visible. A queue that silently
    // dropped it would produce a swipe that stops halfway with no explanation.
    CHECK(!q.push(pointer(InputEventType::PointerMove, 2, 2)));
    CHECK(q.stats().dropped == 1);
    CHECK(q.stats().pushed == InputQueue::kCapacity);
    // The identity, evaluated where `dropped` is non-zero -- which is the only
    // place it can be falsified and the one place nothing was checking it.
    // `test_debug.cpp` asserts the same sum in a case where `dropped` is 0, so
    // it read as coverage of this invariant and was not.
    const auto& s = q.stats();
    CHECK(s.pushed == s.popped + s.flushed + q.size());
}

void peek_shows_the_head_without_taking_it()
{
    InputQueue q;
    CHECK(q.peek() == nullptr);
    CHECK(q.push(pointer(InputEventType::PointerDown, 5, 6)));
    const InputEvent* head = q.peek();
    CHECK(head != nullptr);
    CHECK(head->type == InputEventType::PointerDown);
    CHECK(head->x == 5 && head->y == 6);
    // Looking is not taking: the same event is still there, and still first.
    CHECK(q.peek() != nullptr);
    InputEvent out;
    CHECK(q.pop(out));
    CHECK(out.x == 5 && out.y == 6);
    CHECK(q.peek() == nullptr);
}

void a_selective_consumer_stops_at_the_head_and_keeps_the_order()
{
    // What the simulator's pump does, in the small. A consumer that can take
    // buttons but has run out of room for pointers drains until the head is a
    // pointer, and then stops -- **including** when buttons are queued behind
    // it. `core/input.h` used to claim the opposite. Letting the button past
    // would reorder input, which is the one thing this queue exists to prevent.
    InputQueue q;
    CHECK(q.push(button(InputEventType::ButtonDown, 0)));
    CHECK(q.push(pointer(InputEventType::PointerDown, 1, 1)));
    CHECK(q.push(button(InputEventType::ButtonUp, 0)));

    int taken = 0;
    InputEvent out;
    for (;;) {
        const InputEvent* next = q.peek();
        if (next == nullptr) {
            break;
        }
        const bool needs_slot = next->type == InputEventType::PointerDown ||
                                next->type == InputEventType::PointerMove ||
                                next->type == InputEventType::PointerUp;
        if (needs_slot) {  // stands in for a full transition FIFO
            break;
        }
        CHECK(q.pop(out));
        ++taken;
    }
    // The button in front went through; the pointer and the button behind it
    // are both still queued, in the order they arrived.
    CHECK(taken == 1);
    CHECK(q.size() == 2);
    const InputEvent* head = q.peek();
    CHECK(head != nullptr && head->type == InputEventType::PointerDown);
    CHECK(q.pop(out) && out.type == InputEventType::PointerDown);
    CHECK(q.pop(out) && out.type == InputEventType::ButtonUp);
    CHECK(!q.pop(out));
}

void queue_wraps_without_losing_order()
{
    InputQueue q;
    for (std::size_t i = 0; i < InputQueue::kCapacity; ++i) {
        CHECK(q.push(pointer(InputEventType::PointerMove, static_cast<std::int16_t>(i), 0)));
    }
    // Drain half, refill past the end of the ring, and check the order held.
    for (std::size_t i = 0; i < 40; ++i) {
        InputEvent out;
        CHECK(q.pop(out));
        CHECK(out.x == static_cast<std::int16_t>(i));
    }
    for (std::size_t i = 0; i < 40; ++i) {
        CHECK(q.push(pointer(InputEventType::PointerMove, static_cast<std::int16_t>(100 + i), 0)));
    }
    for (std::size_t i = 40; i < InputQueue::kCapacity; ++i) {
        InputEvent out;
        CHECK(q.pop(out));
        CHECK(out.x == static_cast<std::int16_t>(i));
    }
    for (std::size_t i = 0; i < 40; ++i) {
        InputEvent out;
        CHECK(q.pop(out));
        CHECK(out.x == static_cast<std::int16_t>(100 + i));
    }
    CHECK(q.empty());
}

// --- buttons --------------------------------------------------------------

void a_button_goes_down_and_up()
{
    InputState s;
    CHECK(s.apply(button(InputEventType::ButtonDown, 0), 2));
    CHECK(s.button_down(0));
    CHECK(!s.button_down(1));
    CHECK(s.apply(button(InputEventType::ButtonUp, 0), 2));
    CHECK(!s.button_down(0));
}

void a_button_past_the_profile_is_refused()
{
    InputState s;
    // Two buttons declared; index 2 does not exist on this board. Clamping to
    // the last one would let a test press something and report success while
    // testing a different key entirely.
    CHECK(!s.apply(button(InputEventType::ButtonDown, 2), 2));
    CHECK(!s.button_down(2));
    CHECK(!s.apply(button(InputEventType::ButtonDown, 7), 2));
}

void a_repeated_press_without_a_release_is_refused()
{
    InputState s;
    CHECK(s.apply(button(InputEventType::ButtonDown, 0), 2));
    CHECK(!s.apply(button(InputEventType::ButtonDown, 0), 2));
    CHECK(s.button_down(0));
}

void releasing_a_button_nobody_held_is_refused()
{
    InputState s;
    CHECK(!s.apply(button(InputEventType::ButtonUp, 0), 2));
}

void a_button_release_cannot_cross_origins()
{
    InputState s;
    CHECK(s.apply(button(InputEventType::ButtonDown, 0, InputOrigin::Remote), 2));
    CHECK(!s.apply(button(InputEventType::ButtonDown, 0, InputOrigin::Physical), 2));
    CHECK(!s.apply(button(InputEventType::ButtonUp, 0, InputOrigin::Physical), 2));
    CHECK(s.button_down(0));
    CHECK(s.apply(button(InputEventType::ButtonUp, 0, InputOrigin::Remote), 2));

    CHECK(s.apply(button(InputEventType::ButtonDown, 0, InputOrigin::Physical), 2));
    CHECK(s.apply(button(InputEventType::ButtonUp, 0, InputOrigin::Physical), 2));
}

void latched_power_edges_clear_before_delivery()
{
    using attadipa::firmware::PowerEdgeDelivery;
    using attadipa::firmware::deliver_power_edges;
    using attadipa::firmware::kAxpPowerEdges;
    using attadipa::firmware::kAxpPowerPositiveEdge;

    bool clear_called = false;
    bool clear_failure_reported = false;
    unsigned published = 0;
    auto result = deliver_power_edges(
        kAxpPowerEdges, 1,
        [&](std::uint8_t) {
            clear_called = true;
            return true;
        },
        [&](bool) { ++published; },
        [&] { clear_failure_reported = true; });
    CHECK(result == PowerEdgeDelivery::Deferred);
    CHECK(!clear_called);
    CHECK(!clear_failure_reported);
    CHECK(published == 0);

    result = deliver_power_edges(
        kAxpPowerEdges, 2,
        [&](std::uint8_t edges) {
            clear_called = true;
            CHECK(edges == kAxpPowerEdges);
            return false;
        },
        [&](bool) { ++published; },
        [&] { clear_failure_reported = true; });
    CHECK(result == PowerEdgeDelivery::ClearFailed);
    CHECK(clear_called);
    CHECK(clear_failure_reported);
    CHECK(published == 0);

    clear_failure_reported = false;
    char order[5]{};
    unsigned order_size = 0;
    result = deliver_power_edges(
        kAxpPowerEdges, 2,
        [&](std::uint8_t) {
            order[order_size++] = 'c';
            return true;
        },
        [&](bool pressed) { order[order_size++] = pressed ? 'd' : 'u'; },
        [&] { clear_failure_reported = true; });
    CHECK(result == PowerEdgeDelivery::Delivered);
    CHECK(!clear_failure_reported);
    CHECK(order_size == 3);
    CHECK(order[0] == 'c');
    CHECK(order[1] == 'd');
    CHECK(order[2] == 'u');

    result = deliver_power_edges(
        kAxpPowerPositiveEdge, 1,
        [&](std::uint8_t) {
            order[order_size++] = 'c';
            return true;
        },
        [&](bool pressed) { order[order_size++] = pressed ? 'd' : 'u'; },
        [&] { clear_failure_reported = true; });
    CHECK(result == PowerEdgeDelivery::Delivered);
    CHECK(!clear_failure_reported);
    CHECK(order_size == 5);
    CHECK(order[3] == 'c');
    CHECK(order[4] == 'u');
}

// --- the pointer ----------------------------------------------------------

void a_tap_is_down_then_up()
{
    InputState s;
    CHECK(s.apply(pointer(InputEventType::PointerDown, 30, 40), 2));
    CHECK(s.pointer_down());
    CHECK(s.pointer_x() == 30);
    CHECK(s.pointer_y() == 40);
    CHECK(s.apply(pointer(InputEventType::PointerUp, 30, 40), 2));
    CHECK(!s.pointer_down());
}

void a_swipe_is_down_moves_up_and_the_moves_track()
{
    InputState s;
    CHECK(s.apply(pointer(InputEventType::PointerDown, 200, 180), 2));
    for (std::int16_t x = 190; x >= 50; x -= 10) {
        CHECK(s.apply(pointer(InputEventType::PointerMove, x, 180), 2));
        CHECK(s.pointer_x() == x);
    }
    CHECK(s.apply(pointer(InputEventType::PointerUp, 40, 180), 2));
    CHECK(!s.pointer_down());
    CHECK(s.pointer_x() == 40);
}

void a_second_finger_is_refused_rather_than_merged()
{
    InputState s;
    CHECK(s.apply(pointer(InputEventType::PointerDown, 10, 10, InputOrigin::Remote, 0), 2));
    // touch_id 1 is past kMaxTouchPoints. Refused rather than replacing the
    // first point, which would look like an impossibly fast drag.
    CHECK(!s.apply(pointer(InputEventType::PointerDown, 90, 90, InputOrigin::Remote, 1), 2));
    CHECK(s.pointer_x() == 10);
    // A second point on the *same* id is refused too: the finger is already down.
    CHECK(!s.apply(pointer(InputEventType::PointerDown, 90, 90, InputOrigin::Remote, 0), 2));
    CHECK(s.pointer_x() == 10);
}

void a_move_with_nothing_down_is_refused()
{
    InputState s;
    // Neither panel reports a hover; both touch controllers are contact
    // devices. A move accepted here would become a phantom drag.
    CHECK(!s.apply(pointer(InputEventType::PointerMove, 50, 50), 2));
    CHECK(!s.pointer_down());
}

void a_release_of_a_different_touch_id_is_refused()
{
    InputState s;
    CHECK(s.apply(pointer(InputEventType::PointerDown, 10, 10, InputOrigin::Remote, 0), 2));
    CHECK(!s.apply(pointer(InputEventType::PointerUp, 10, 10, InputOrigin::Remote, 1), 2));
    CHECK(s.pointer_down());
}

// --- cleanup --------------------------------------------------------------

void release_all_lifts_what_the_remote_held()
{
    InputState s;
    InputQueue q;

    CHECK(s.apply(button(InputEventType::ButtonDown, 0, InputOrigin::Remote), 2));
    CHECK(s.apply(pointer(InputEventType::PointerDown, 77, 88, InputOrigin::Remote), 2));

    const std::uint8_t written = s.release_all(InputOrigin::Remote, 1234, q);
    CHECK(written == 2);

    InputEvent first;
    CHECK(q.pop(first));
    CHECK(first.type == InputEventType::ButtonUp);
    CHECK(first.button == 0);
    CHECK(first.at_ms == 1234);

    InputEvent second;
    CHECK(q.pop(second));
    CHECK(second.type == InputEventType::PointerUp);
    // Lifted where the finger actually is, not at the origin of the screen.
    CHECK(second.x == 77);
    CHECK(second.y == 88);
}

void release_all_leaves_a_persons_finger_alone()
{
    InputState s;
    InputQueue q;

    CHECK(s.apply(button(InputEventType::ButtonDown, 0, InputOrigin::Physical), 2));
    CHECK(s.apply(button(InputEventType::ButtonDown, 1, InputOrigin::Remote), 2));
    CHECK(s.apply(pointer(InputEventType::PointerDown, 5, 5, InputOrigin::Physical), 2));

    const std::uint8_t written = s.release_all(InputOrigin::Remote, 10, q);
    CHECK(written == 1);

    InputEvent out;
    CHECK(q.pop(out));
    CHECK(out.type == InputEventType::ButtonUp);
    CHECK(out.button == 1);
    CHECK(!q.pop(out));
}

void release_all_on_an_idle_state_writes_nothing()
{
    InputState s;
    InputQueue q;
    CHECK(s.release_all(InputOrigin::Remote, 1, q) == 0);
    CHECK(q.empty());
}

}  // namespace

int main()
{
    queue_is_first_in_first_out();
    queue_counts_an_overrun_instead_of_hiding_it();
    queue_wraps_without_losing_order();
    peek_shows_the_head_without_taking_it();
    a_selective_consumer_stops_at_the_head_and_keeps_the_order();

    a_button_goes_down_and_up();
    a_button_past_the_profile_is_refused();
    a_repeated_press_without_a_release_is_refused();
    releasing_a_button_nobody_held_is_refused();
    a_button_release_cannot_cross_origins();
    latched_power_edges_clear_before_delivery();

    a_tap_is_down_then_up();
    a_swipe_is_down_moves_up_and_the_moves_track();
    a_second_finger_is_refused_rather_than_merged();
    a_move_with_nothing_down_is_refused();
    a_release_of_a_different_touch_id_is_refused();

    release_all_lifts_what_the_remote_held();
    release_all_leaves_a_persons_finger_alone();
    release_all_on_an_idle_state_writes_nothing();

    if (failures != 0) {
        std::fprintf(stderr, "%d input check(s) failed\n", failures);
        return 1;
    }
    std::printf("input: all checks passed\n");
    return 0;
}
