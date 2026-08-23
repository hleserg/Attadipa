#pragma once

#include <vector>

#include "attadipa/core/input.h"
#include "attadipa/debug/bridge.h"
#include "attadipa/platform/board_profile.h"

namespace attadipa::sim {

// The simulator's answer to "give me the frame that is on the screen".
//
// It uses LVGL's own snapshot API rather than reading back a driver's buffer.
// That is the right choice here and would be the right choice on a device for
// the same reason: `lv_snapshot_take` re-renders the object tree into a fresh
// buffer, so what comes out is one internally consistent frame. Reading a
// display driver's partial draw buffers would give whichever rectangles
// happened to be in flight, and reading pixels back out of the panel is not
// available at all -- the AMOLED sits behind QSPI and its controller is not
// established to support a read path (D7 has not even settled the init
// sequence).
//
// The pixels are reported as **Bgr888**, which is what LVGL's RGB888 actually
// is in memory. Converting here and calling it RGB would work until somebody
// changed LVGL's colour depth; naming the real layout on the wire means the
// host converter is the only place that has to know, and it is tested against
// both orders.

// The three button maxima live in three libraries that do not include one
// another -- `platform` does not link `core`, `debug` does not link
// `platform` -- so nothing could assert their relationship until here. This is
// the one translation unit that sees all three, which makes it the only place
// a raised bound can be made to fail at compile time instead of at runtime.
// `core/input.h` points at these.
static_assert(platform::kMaxBoardButtons <= core::kMaxButtons,
              "a board may not declare more buttons than the input layer can hold");

class LvglScreenSource : public debug::ScreenSource {
public:
    explicit LvglScreenSource(const platform::BoardProfile& board);

    bool capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                 std::uint16_t& height_out, debug::PixelFormat& format_out,
                 debug::Orientation& orientation_out, std::size_t& bytes_out,
                 debug::ScreenSource::Failure& why_out) override;

    const char*                    board_id() const override { return board_.id; }
    const char*                    build_id() const override;
    std::uint8_t                   button_count() const override { return button_count_; }
    const debug::ButtonDescriptor* buttons() const override { return buttons_; }

    // The interface has been idle for at least `ms` -- a duration the caller
    // chooses, which `WaitStable` now carries in its body.
    //
    // Real, not a constant: LVGL tracks the last input activity, so the
    // simulator can answer the question `WaitStable` actually asks. Two things
    // had to be true for that, and only the first one was: the source must
    // measure something, *and* the caller must pass a duration rather than the
    // monotonic tick. Passing `now_ms` reduced this to "idle since boot",
    // which is true before the first input and false forever after. A vacuous
    // step in a test harness reads as a passing one, at either end.
    bool stable_since(std::uint32_t ms) const override;

    // The frame buffer the bridge needs, sized for this board's panel.
    std::size_t frame_bytes() const;

private:
    platform::BoardProfile  board_;
    debug::ButtonDescriptor buttons_[4]  = {};
    std::uint8_t            button_count_ = 0;

    static_assert(platform::kMaxBoardButtons <= sizeof(buttons_) / sizeof(buttons_[0]),
                  "the wire struct's button array is smaller than a board may declare");
};

}  // namespace attadipa::sim
