#pragma once

#include <vector>

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
class LvglScreenSource : public debug::ScreenSource {
public:
    explicit LvglScreenSource(const platform::BoardProfile& board);

    bool capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                 std::uint16_t& height_out, debug::PixelFormat& format_out,
                 debug::Orientation& orientation_out, std::size_t& bytes_out) override;

    const char*                    board_id() const override { return board_.id; }
    const char*                    build_id() const override;
    std::uint8_t                   button_count() const override { return button_count_; }
    const debug::ButtonDescriptor* buttons() const override { return buttons_; }

    // The frame buffer the bridge needs, sized for this board's panel.
    std::size_t frame_bytes() const;

private:
    platform::BoardProfile  board_;
    debug::ButtonDescriptor buttons_[4]  = {};
    std::uint8_t            button_count_ = 0;
};

}  // namespace attadipa::sim
