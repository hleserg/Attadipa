#pragma once

#include <cstddef>
#include <cstdint>

// The input layer: what a finger and a button produce, before anything knows
// which widget is under them.
//
// This did not exist. Nothing under core/, platform/, ui/ or apps/ modelled a
// press or a touch, because until now the only thing driving the interface was
// a person in front of the simulator and LVGL's own SDL driver. That was fine
// while a human was the only source of input, and stops being fine the moment
// a second source appears — a debug bridge injecting a swipe, and later a real
// touch controller and a real button.
//
// The rule that shapes this file: **every source this project writes a driver
// for pushes into one queue**, and the interface cannot tell them apart. A
// remote tap and a finger tap arrive as the same event, in the same order, in
// the same space. The alternative — a debug path that calls screen handlers
// directly — produces tests that pass against code a user's finger would never
// reach, which is worse than no tests because it reads as coverage.
//
// **Today that is one producer: the debug bridge.** The simulator's finger is
// LVGL's own `lv_sdl_mouse_create()`, which reads SDL and reaches the
// interface as its own indev without passing through here — an explicit
// exception, not an oversight, and the reason `InputOrigin::Physical` has no
// producer outside the tests yet. The touch controller and the buttons are
// T-114's, and they are what this side of the enum is for. Until then, do not
// read this file as saying the simulator's mouse is in the queue: it is not,
// and a claim that outruns the code is the thing this repository's review gate
// exists to catch.
//
// Two properties this queue guarantees, both because the far end is an MCU:
//
//   1. **It allocates nothing.** Fixed capacity, declared here, sized as
//      RESOURCE_BUDGET section 4 requires of any pool.
//   2. **An overrun is counted, never silent.** A dropped event that nobody
//      counted is a UI bug that reproduces once a week and cannot be explained.
//
// Coordinates are in the **logical** coordinate space of the active screen —
// the same space LVGL lays out in, after rotation. A source that reads a
// touch controller in panel coordinates converts before it pushes; that
// conversion belongs with the board, not here, and not in the host tool.

namespace attadipa::core {

// Where an event came from. Not for the interface — which must never branch on
// it — but for cleanup: when a debug connection drops mid-swipe, the finger it
// was holding down has to be lifted, and the physical button a person is
// genuinely holding must not be.
enum class InputOrigin : std::uint8_t { Physical, Remote };

enum class InputEventType : std::uint8_t {
    ButtonDown,
    ButtonUp,
    PointerDown,
    PointerMove,
    PointerUp,
};

// The largest number of buttons any profile may declare.
//
// Three, because the T-Watch's three named inputs (PWR through the PMU, and
// BOOT and RST on the GNSS daughterboard) is the largest set either board is
// known to have, and the Waveshare's count is two. A profile that needs more
// changes this constant **and** `platform::kMaxBoardButtons` **and** the wire
// struct's `buttons[]` bound, which are three numbers in three libraries that
// do not include one another. `sim/screen_source.h` is the one translation
// unit that sees all three, and it carries the `static_assert` that ties them
// together — that is where a raised bound fails to compile, on purpose.
inline constexpr std::uint8_t kMaxButtons = 3;

// One touch point.
//
// **Not a multitouch limit chosen for convenience — a real one.** LVGL's
// pointer input device reads a single coordinate pair per device
// (`lv_indev_data_t` has one `point`), so nothing above this layer could
// consume a second finger even if a controller reported one. What the touch
// controller on either board can do is a separate question and still UNKNOWN:
// the Waveshare's part number behind chip ID 0x64 is open as T-113 and no
// FT3168 datasheet has been obtained. So this is a limitation of the graphics
// stack, asserted about the graphics stack, and it is why `touch_id` exists in
// the wire protocol and is refused here rather than being absent: the field
// survives to a device that can use it, and until then a second finger is an
// error instead of a silent merge into the first.
inline constexpr std::uint8_t kMaxTouchPoints = 1;

struct InputEvent {
    InputEventType type      = InputEventType::PointerUp;
    InputOrigin    origin    = InputOrigin::Physical;

    // ButtonDown / ButtonUp. An index into the board profile's button list, so
    // that no enumeration in this file has to claim which physical key it is —
    // on the Waveshare, which named input each of the two pressable buttons
    // reaches is open question D5.
    std::uint8_t button = 0;

    // Pointer events. Logical screen coordinates.
    std::int16_t x        = 0;
    std::int16_t y        = 0;
    std::uint8_t touch_id = 0;

    // Milliseconds on the device's own monotonic clock, filled by the source.
    // Carried rather than derived because a swipe's *speed* is what decides
    // whether a gesture recogniser sees a fling or a drag, and a queue that
    // timestamps on drain reports the queue's latency instead of the finger's.
    std::uint32_t at_ms = 0;
};

struct InputQueueStats {
    std::uint32_t pushed  = 0;
    std::uint32_t popped  = 0;
    std::uint32_t dropped = 0;  // queue was full. Never silent.
    std::uint32_t flushed = 0;  // discarded by clear(). Also never silent.
};

// `pushed == popped + dropped + flushed + size()` holds at every moment. That
// identity is the whole point of the counters: a number that does not add up
// is a lost event, and a lost input event is a UI bug that reproduces once a
// week and cannot be explained.

// A fixed-capacity single-producer-friendly ring of input events.
//
// Capacity is 64: a 500 ms swipe sampled at 60 Hz is 32 points, so one full
// gesture fits with room to spare even if the interface stalls for a frame.
// Deliberately not larger — a queue deep enough to hide a stall will hide it.
class InputQueue {
public:
    static constexpr std::size_t kCapacity = 64;

    // Returns false if the queue was full; the event is dropped and counted.
    bool push(const InputEvent& event);

    // Takes the oldest event. Returns false when empty.
    bool pop(InputEvent& out);

    // Discards everything, counting what it discarded. Used by the tests and
    // available to a caller that has decided the queued past is meaningless;
    // note that `input reset` does **not** use it — that path lifts what is
    // held through `InputState::release_all`, because a wedged widget needs
    // the release *delivered*, not the release *forgotten*.
    void clear();

    bool        empty() const { return count_ == 0; }
    std::size_t size() const { return count_; }

    const InputQueueStats& stats() const { return stats_; }

private:
    InputEvent      buffer_[kCapacity] = {};
    std::size_t     head_              = 0;
    std::size_t     count_             = 0;
    InputQueueStats stats_{};
};

// What is currently held down, and by whom.
//
// The interface does not read this. It exists so that a dropped connection can
// be made harmless: section 10 of the owner's request asks for a safe reset of
// stuck virtual touches and held buttons, and "safe" means releasing exactly
// what the remote held and nothing a person is holding.
class InputState {
public:
    // Applies an event. Returns false if the event is impossible from the
    // current state — a second finger down, a button released that was not
    // held, a button index past the profile — in which case nothing changed.
    // The caller reports the error; this class never guesses an intent.
    bool apply(const InputEvent& event, std::uint8_t button_count);

    bool button_down(std::uint8_t index) const;
    bool pointer_down() const { return pointer_down_; }

    std::int16_t pointer_x() const { return pointer_x_; }
    std::int16_t pointer_y() const { return pointer_y_; }

    InputOrigin pointer_origin() const { return pointer_origin_; }

    // The id of the finger currently down. Needed because `apply` refuses a
    // `PointerUp` whose `touch_id` does not match, so anything synthesising a
    // release -- the hold expiry, a disconnect -- has to ask rather than assume
    // zero. That assumption is true only while `kMaxTouchPoints == 1`, and the
    // field exists precisely so it survives to a device where it is not.
    std::uint8_t pointer_touch_id() const { return pointer_touch_id_; }
    InputOrigin button_origin(std::uint8_t index) const;

    // Writes the events needed to lift everything `origin` is holding into
    // `queue`, oldest-first, and returns how many were written. A no-op when
    // that origin holds nothing, which is the common case and must stay cheap.
    //
    // An input is marked released **only if its event reached the queue**. If
    // the queue is full the input stays held on purpose: the widget under it
    // still believes it is pressed, so forgetting about it here would strand
    // that belief with nothing left to correct it. The queue counts the drop,
    // and the debug bridge's hold expiry retries.
    std::uint8_t release_all(InputOrigin origin, std::uint32_t at_ms, InputQueue& queue);

private:
    bool         button_held_[kMaxButtons]   = {};
    InputOrigin  button_origin_[kMaxButtons] = {};
    bool         pointer_down_               = false;
    std::int16_t pointer_x_                  = 0;
    std::int16_t pointer_y_                  = 0;
    std::uint8_t pointer_touch_id_           = 0;
    InputOrigin  pointer_origin_             = InputOrigin::Physical;
};

}  // namespace attadipa::core
