#include "screen_source.h"

#include <cstdio>
#include <cstring>

#include "lvgl.h"

#include "attadipa/version.h"

namespace attadipa::sim {

LvglScreenSource::LvglScreenSource(const platform::BoardProfile& board) : board_(board)
{
    const std::uint8_t count =
        board.button_count > 4 ? 4 : board.button_count;
    for (std::uint8_t i = 0; i < count; ++i) {
        std::strncpy(buttons_[i].id, board.buttons[i].id, sizeof(buttons_[i].id) - 1);
        buttons_[i].flags = 0;
        if (board.buttons[i].injectable) {
            buttons_[i].flags |= debug::kButtonInjectable;
        }
        // Carried to the host so that a tool can print it. "Two buttons, and
        // nobody has established what either does" is a fact worth showing a
        // person who is about to press one -- D5.
        if (!board.buttons[i].role_known) {
            buttons_[i].flags |= debug::kButtonRoleUnknown;
        }
    }
    button_count_ = count;
}

const char* LvglScreenSource::build_id() const
{
    // Deliberately says "simulator". A host tool that cannot tell a simulated
    // frame from a photographed panel would let a screenshot be filed as
    // hardware evidence, which is the one thing CLAUDE.md forbids outright.
    static char build[24] = {};
    if (build[0] == '\0') {
        std::snprintf(build, sizeof(build), "sim %s", ATTADIPA_VERSION_STRING);
    }
    return build;
}

std::size_t LvglScreenSource::frame_bytes() const
{
    return static_cast<std::size_t>(board_.display.width_px) * board_.display.height_px * 3u;
}

bool LvglScreenSource::capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                               std::uint16_t& height_out, debug::PixelFormat& format_out,
                               debug::Orientation& orientation_out, std::size_t& bytes_out)
{
    width_out  = board_.display.width_px;
    height_out = board_.display.height_px;
    format_out = debug::PixelFormat::Bgr888;
    // The simulator draws at the panel's native resolution with no rotation
    // applied, so the framebuffer is already the way the panel is worn. A
    // device driver that rotates will report something else here, and the host
    // will apply it -- which is why the field is on the wire rather than being
    // a constant the tool holds.
    orientation_out = debug::Orientation::Deg0;
    bytes_out       = frame_bytes();

    // A metadata-only query: capabilities asks the shape without wanting the
    // pixels.
    if (out == nullptr || capacity == 0) {
        return false;
    }
    if (capacity < bytes_out) {
        return false;
    }

    lv_draw_buf_t* snapshot = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (snapshot == nullptr) {
        bytes_out = 0;
        return false;
    }

    const std::uint32_t width  = snapshot->header.w;
    const std::uint32_t height = snapshot->header.h;
    const std::uint32_t stride = snapshot->header.stride;

    // The snapshot is of the active screen, which should be the panel size. If
    // it is not -- a screen smaller than the display, say -- reporting the
    // board's dimensions with the snapshot's pixels would produce a skewed
    // image that still looked like a picture. Refuse instead.
    if (width != board_.display.width_px || height != board_.display.height_px) {
        lv_draw_buf_destroy(snapshot);
        bytes_out = 0;
        return false;
    }

    // Row by row, because the snapshot's stride is padded and the wire format
    // is tightly packed. Copying `stride * height` and calling it the image is
    // how a screenshot acquires a diagonal skew.
    for (std::uint32_t y = 0; y < height; ++y) {
        std::memcpy(out + static_cast<std::size_t>(y) * width * 3u,
                    snapshot->data + static_cast<std::size_t>(y) * stride, width * 3u);
    }

    lv_draw_buf_destroy(snapshot);
    bytes_out = static_cast<std::size_t>(width) * height * 3u;
    return true;
}

}  // namespace attadipa::sim
