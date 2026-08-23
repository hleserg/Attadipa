#include "remote_input.h"

#include "lvgl.h"

#include <cstddef>

namespace attadipa::sim {
namespace {

core::InputQueue* g_queue = nullptr;
InputListener     g_button_listener  = nullptr;
InputListener     g_pointer_listener = nullptr;

// What LVGL should see, one transition at a time.
//
// **LVGL reads an input device every `LV_DEF_REFR_PERIOD`, which this build
// sets to 33 ms** (`sim/lv_conf_simulator.h`, and `lv_indev.c:132` creates the
// read timer with it) -- *not* once per `lv_timer_handler` call. The simulator
// loop runs at 5 ms while a client is connected, so roughly six or seven pumps
// happen between two reads. An earlier version of this file kept a single
// `press_pending`/`release_pending` pair across that gap, which silently
// coalesced: two taps arriving inside one 33 ms window produced **one** click,
// at the second tap's coordinates. Worse, the diagnostic screen draws its trail
// from the queue drain rather than from LVGL, so the screenshot showed all four
// points landing while the interface had received one click -- the tool
// manufacturing the exact defect it exists to rule out, and timing-dependent
// enough to read as a flaky UI bug rather than a tool bug.
//
// The fix is LVGL's own idiom: a FIFO of transitions, one reported per read
// callback, with `data->continue_reading` set while more remain. LVGL's
// `do { indev_read_core(); ... indev_pointer_proc(); } while (continue_reading)`
// (`lv_indev.c:253-287`) then processes every one of them inside the same
// 33 ms tick, in order, with each press and release actually dispatched.
struct PointerTransition {
    std::int16_t x       = 0;
    std::int16_t y       = 0;
    bool         pressed = false;
};

// Sized to the input queue: one pump can produce at most one transition per
// queued event, and the bridge's own rate cap (500/s) keeps a 33 ms window far
// below this. When it is full the pump stops draining rather than overwriting,
// so backpressure lands in the queue -- which counts -- instead of silently
// eating a swipe point here.
constexpr std::size_t     kTransitionCapacity = core::InputQueue::kCapacity;

// How many transitions one `lv_indev_read` burst may dispatch. Sixteen at the
// 33 ms read period is ~480 points a second, far above a finger and above the
// swipe interpolation this tool generates, so it bounds the pathological case
// without slowing a real gesture.
constexpr std::size_t     kMaxTransitionsPerRead = 16;
std::size_t               g_read_burst           = 0;
PointerTransition         g_transitions[kTransitionCapacity];
std::size_t               g_transition_head  = 0;
std::size_t               g_transition_count = 0;

// The state LVGL last saw, held between transitions so that a read with an
// empty FIFO repeats the current truth rather than inventing a release.
bool         g_pointer_pressed = false;
std::int16_t g_pointer_x       = 0;
std::int16_t g_pointer_y       = 0;

bool transitions_full()
{
    return g_transition_count == kTransitionCapacity;
}

// The press state *after* everything already in the FIFO -- the state a move
// arriving now should carry. Reading `g_pointer_pressed` instead would use the
// state LVGL has reached, which lags whatever is still queued.
bool pending_pressed()
{
    if (g_transition_count == 0) {
        return g_pointer_pressed;
    }
    const std::size_t last = (g_transition_head + g_transition_count - 1) % kTransitionCapacity;
    return g_transitions[last].pressed;
}

void push_transition(std::int16_t x, std::int16_t y, bool pressed)
{
    if (transitions_full()) {
        return;
    }
    const std::size_t at = (g_transition_head + g_transition_count) % kTransitionCapacity;
    g_transitions[at]    = PointerTransition{x, y, pressed};
    ++g_transition_count;
}

void read_pointer(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;

    if (g_transition_count > 0) {
        const PointerTransition& next = g_transitions[g_transition_head];
        g_pointer_x                   = next.x;
        g_pointer_y                   = next.y;
        g_pointer_pressed             = next.pressed;
        g_transition_head             = (g_transition_head + 1) % kTransitionCapacity;
        --g_transition_count;

        // Come straight back for the next one. Without this each transition
        // would wait 33 ms for its own tick, and a 24-point swipe would take
        // most of a second to reach the interface.
        //
        // `data->timestamp` is deliberately left to LVGL. A client replaying a
        // recorded gesture sends its own `at_ms`, which the queued event keeps
        // for a gesture recogniser -- but LVGL uses this field for
        // `last_activity_time`, and a timestamp from another epoch would break
        // the inactivity clock the screen dimming will later rest on.
        // Bounded. `continue_reading` used to be "is there another one", which
        // lets a single `lv_indev_read` dispatch all 64 queued transitions --
        // and every widget event they fire -- inside one `lv_timer_handler`.
        // That is the pause the screenshot is chunked to avoid
        // (`bridge.h:139-145`), reintroduced next door. The remainder is not
        // lost, it waits for the next 33 ms read, and a burst larger than this
        // is a client outrunning the interface rather than a gesture.
        ++g_read_burst;
        if (g_transition_count == 0 || g_read_burst >= kMaxTransitionsPerRead) {
            data->continue_reading = false;
            g_read_burst           = 0;
        } else {
            data->continue_reading = true;
        }
    }

    data->point.x = g_pointer_x;
    data->point.y = g_pointer_y;
    data->state   = g_pointer_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

void remote_input_attach(core::InputQueue& queue)
{
    g_queue = &queue;

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_pointer);
}

void remote_input_set_button_listener(InputListener listener)
{
    g_button_listener = listener;
}

void remote_input_set_pointer_listener(InputListener listener)
{
    g_pointer_listener = listener;
}

void remote_input_pump()
{
    if (g_queue == nullptr) {
        return;
    }

    // Drains everything the queue holds. The one-event-per-pump break this
    // loop used to carry was there to stop a swipe collapsing into a jump; the
    // transition FIFO above does that job properly now, and does it where LVGL
    // can actually see each step. Buttons never went through LVGL and still do
    // not -- the project has not decided what its buttons mean (D5), so they
    // reach listeners rather than an `lv_group`.
    //
    // The FIFO-full check is per event rather than on the loop, and that is the
    // whole of this paragraph's reason to exist. Guarding the drain meant a full
    // pointer FIFO also stopped every **button** behind it -- and a button
    // consumes no FIFO slot at all. Reachable inside the protocol's own limits:
    // the bridge permits 500 events a second and about 485 drain, so sustained
    // injection fills the 64-deep FIFO in a few seconds and the buttons stop
    // with it. Peeking keeps the pointer event in the queue for the next pump:
    // delayed, never dropped.
    core::InputEvent event;
    for (;;) {
        const core::InputEvent* next = g_queue->peek();
        if (next == nullptr) {
            break;
        }
        const bool needs_slot = next->type == core::InputEventType::PointerDown ||
                                next->type == core::InputEventType::PointerMove ||
                                next->type == core::InputEventType::PointerUp;
        if (needs_slot && transitions_full()) {
            break;
        }
        if (!g_queue->pop(event)) {
            break;
        }
        switch (event.type) {
        case core::InputEventType::PointerDown:
            push_transition(event.x, event.y, true);
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::PointerMove:
            // A move carries the press state forward: LVGL learns a drag from
            // a sequence of pressed points at moving coordinates, so reporting
            // a move as released would end the gesture mid-swipe.
            push_transition(event.x, event.y, pending_pressed());
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::PointerUp:
            push_transition(event.x, event.y, false);
            if (g_pointer_listener != nullptr) {
                g_pointer_listener(event);
            }
            break;
        case core::InputEventType::ButtonDown:
        case core::InputEventType::ButtonUp:
            if (g_button_listener != nullptr) {
                g_button_listener(event);
            }
            break;
        }
    }
}

}  // namespace attadipa::sim
