#include "watch_control.h"

#include "physical_input.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "attadipa/core/input.h"
#include "attadipa/debug/bridge.h"
#include "attadipa/link/frame_codec.h"
#include "attadipa/platform/board_profile.h"

namespace {

constexpr char kTag[] = "watch-control";
constexpr char kBoardProfileId[] = "waveshare-amoled-206";
constexpr std::size_t kOutputCapacity = 16 * 1024;
constexpr std::size_t kMaxFramesPerPoll = 64;
constexpr std::size_t kMaxUsbWritesPerPoll = 8;
constexpr std::uint32_t kPollMs = 5;

class FirmwareScreenSource final : public attadipa::debug::ScreenSource {
public:
  FirmwareScreenSource(const attadipa::platform::BoardProfile &board,
                       const attadipa::core::InputQueue &queue)
      : board_(board), queue_(queue) {
    const std::uint8_t count =
        std::min(board.button_count, attadipa::platform::kMaxBoardButtons);
    for (std::uint8_t i = 0; i < count; ++i) {
      std::strncpy(buttons_[i].id, board.buttons[i].id,
                   sizeof(buttons_[i].id) - 1);
      if (board.buttons[i].injectable) {
        buttons_[i].flags |= attadipa::debug::kButtonInjectable;
      }
      if (!board.buttons[i].role_known) {
        buttons_[i].flags |= attadipa::debug::kButtonRoleUnknown;
      }
    }
    button_count_ = count;
    std::snprintf(build_, sizeof(build_), "device %.16s",
                  esp_app_get_description()->version);
  }

  bool capture(std::uint8_t *out, std::size_t capacity,
               std::uint16_t &width_out, std::uint16_t &height_out,
               attadipa::debug::PixelFormat &format_out,
               attadipa::debug::Orientation &orientation_out,
               std::size_t &bytes_out, Failure &why_out) override {
    width_out = board_.display.width_px;
    height_out = board_.display.height_px;
    format_out = attadipa::debug::PixelFormat::Rgb565Le;
    orientation_out = attadipa::debug::Orientation::Deg0;
    bytes_out = frame_bytes();
    why_out = Failure::None;

    if (out == nullptr || capacity == 0) {
      why_out = Failure::ShapeQuery;
      return false;
    }
    if (capacity < bytes_out) {
      why_out = Failure::BufferTooSmall;
      return false;
    }

    lv_draw_buf_t snapshot{};
    if (lv_draw_buf_init(&snapshot, width_out, height_out,
                         LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO, out,
                         capacity) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                     &snapshot) != LV_RESULT_OK) {
      bytes_out = 0;
      why_out = Failure::RendererFailed;
      return false;
    }
    if (snapshot.header.w != width_out || snapshot.header.h != height_out) {
      bytes_out = 0;
      why_out = Failure::GeometryMismatch;
      return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(width_out) * 2;
    if (snapshot.header.stride < row_bytes) {
      bytes_out = 0;
      why_out = Failure::GeometryMismatch;
      return false;
    }
    if (snapshot.header.stride != row_bytes) {
      for (std::uint16_t y = 1; y < height_out; ++y) {
        std::memmove(out + static_cast<std::size_t>(y) * row_bytes,
                     out + static_cast<std::size_t>(y) * snapshot.header.stride,
                     row_bytes);
      }
    }
    bytes_out = frame_bytes();
    return true;
  }

  const char *board_id() const override { return board_.id; }
  const char *build_id() const override { return build_; }
  std::uint8_t button_count() const override { return button_count_; }
  const attadipa::debug::ButtonDescriptor *buttons() const override {
    return buttons_;
  }
  bool stable_since(std::uint32_t ms) const override {
    return queue_.empty() && lv_anim_count_running() == 0 &&
           lv_display_get_inactive_time(nullptr) >= ms;
  }
  std::size_t frame_bytes() const {
    return static_cast<std::size_t>(board_.display.width_px) *
           board_.display.height_px * 2;
  }

private:
  attadipa::platform::BoardProfile board_;
  const attadipa::core::InputQueue &queue_;
  attadipa::debug::ButtonDescriptor buttons_[4]{};
  std::uint8_t button_count_ = 0;
  char build_[24]{};
};

class WatchControl {
public:
  WatchControl(const attadipa::platform::BoardProfile &board,
               attadipa::core::InputQueue &input_queue,
               attadipa::core::InputState &input_state,
               attadipa::debug::TimeSink *time_sink,
               attadipa::debug::MeshSink *mesh_sink,
               std::uint8_t *frame,
               std::size_t frame_capacity)
      : board_(board), source_(board_, input_queue),
        bridge_(input_queue, input_state, source_, frame, frame_capacity,
                time_sink, mesh_sink),
        sleep_cycles_seen_(physical_input_sleep_cycles()) {}

  esp_err_t attach() {
    lv_timer_t *timer = lv_timer_create(poll_timer, kPollMs, this);
    if (timer == nullptr) {
      return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
  }

private:
  static void emit(void *ctx, const std::uint8_t *payload, std::size_t length) {
    static_cast<WatchControl *>(ctx)->queue(payload, length);
  }

  static void poll_timer(lv_timer_t *timer) {
    static_cast<WatchControl *>(lv_timer_get_user_data(timer))->poll();
  }


  std::size_t output_free() const { return output_.size() - output_count_; }

  void queue(const std::uint8_t *payload, std::size_t length) {
    std::uint8_t frame[attadipa::link::kMaxFrame]{};
    const std::size_t bytes =
        attadipa::link::encode(payload, length, frame, sizeof(frame));
    if (bytes == 0 || bytes > output_free()) {
      overflowed_ = true;
      return;
    }
    if (output_head_ + output_count_ + bytes > output_.size()) {
      std::memmove(output_.data(), output_.data() + output_head_,
                   output_count_);
      output_head_ = 0;
    }
    std::memcpy(output_.data() + output_head_ + output_count_, frame, bytes);
    output_count_ += bytes;
  }

  void flush() {
    // One driver write per complete frame. Console logs share this USB stream;
    // letting a write boundary cut through a frame lets a concurrent log land
    // inside its CRC span. Whole-frame writes keep logs between frames, where
    // the resynchronising decoder is designed to discard them.
    for (std::size_t i = 0; i < kMaxUsbWritesPerPoll &&
                            output_count_ >= attadipa::link::kHeaderBytes;
         ++i) {
      const std::uint8_t *frame = output_.data() + output_head_;
      const std::size_t frame_bytes =
          static_cast<std::size_t>(frame[2] | (frame[3] << 8)) +
          attadipa::link::kOverheadBytes;
      if (frame_bytes > output_count_) {
        overflowed_ = true;
        return;
      }
      const int written = usb_serial_jtag_write_bytes(frame, frame_bytes, 0);
      if (written == 0) {
        break;
      }
      if (written != static_cast<int>(frame_bytes)) {
        overflowed_ = true;
        return;
      }
      output_head_ += frame_bytes;
      output_count_ -= frame_bytes;
      if (output_count_ == 0) {
        output_head_ = 0;
      }
    }
  }

  void dispatch(std::uint32_t now_ms) {
    std::uint8_t payload[attadipa::link::kMaxPayload]{};
    while (output_free() >= attadipa::link::kMaxFrame) {
      const attadipa::link::FrameResult frame =
          decoder_.next(payload, sizeof(payload));
      if (frame.exhausted()) {
        return;
      }
      if (frame.status == attadipa::link::FrameStatus::OutputTooSmall) {
        overflowed_ = true;
        return;
      }
      bridge_.handle(payload, frame.length, now_ms, emit, this);
    }
  }

  // A HELLO from the host is the other session boundary, and it does not come
  // through here: `Bridge::handle_hello` ends the bridge's session itself and
  // echoes the host's generation. This queue and the driver's TX ring are
  // deliberately left alone for it. Everything in them was written before the
  // `HelloOk`, and the host discards whatever it reads ahead of the `HelloOk`
  // carrying its generation -- so bytes the driver has already accepted need
  // no discarding, atomic or otherwise (#348).
  void reset_remote(std::uint32_t now_ms, const char *reason) {
    decoder_.reset();
    output_head_ = 0;
    output_count_ = 0;
    overflowed_ = false;
    bridge_.on_disconnect(now_ms);
    ESP_LOGW(kTag, "%s; reset remote state", reason);
  }

  void poll() {
    const std::uint32_t now_ms =
        static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    // Sleep belongs to the physical path now, and it does not call into here.
    // A cycle it completed while this timer was not running invalidates the
    // session exactly as a disconnect does, so read the counter and treat a
    // change as one. Clearing usb_connected_ here matches what the old
    // in-line reset did: a host that is still there is re-seen below, and a
    // host that left does not also produce a second "disconnected" reset.
    const std::uint32_t sleep_cycles = physical_input_sleep_cycles();
    if (sleep_cycles != sleep_cycles_seen_) {
      sleep_cycles_seen_ = sleep_cycles;
      reset_remote(now_ms, "light-sleep cycle completed");
      usb_connected_ = false;
    }
    const bool usb_connected = usb_serial_jtag_is_connected();
    if (!usb_connected) {
      if (usb_connected_) {
        reset_remote(now_ms, "USB host disconnected");
      }
      usb_connected_ = false;
      bridge_.tick(now_ms, emit, this);
      return;
    }
    usb_connected_ = true;
    flush();

    std::uint8_t bytes[1024]{};
    const int received = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
    if (received > 0) {
      std::size_t at = 0;
      const std::size_t total = static_cast<std::size_t>(received);
      while (at < total) {
        dispatch(now_ms);
        const std::size_t accepted = decoder_.push(bytes + at, total - at);
        if (accepted == 0) {
          break;
        }
        at += accepted;
      }
      dispatch(now_ms);
    }

    for (std::size_t i = 0;
         i < kMaxFramesPerPoll && output_free() >= attadipa::link::kMaxFrame;
         ++i) {
      if (!bridge_.pump(emit, this)) {
        break;
      }
    }
    bridge_.tick(now_ms, emit, this);
    flush();

    if (overflowed_) {
      reset_remote(now_ms, "USB client overran the bounded output queue");
    }
  }





  const attadipa::platform::BoardProfile board_;
  FirmwareScreenSource source_;
  attadipa::debug::Bridge bridge_;
  attadipa::link::Decoder decoder_{};
  std::array<std::uint8_t, kOutputCapacity> output_{};
  std::size_t output_head_ = 0;
  std::size_t output_count_ = 0;
  bool overflowed_ = false;
  bool usb_connected_ = false;
  std::uint32_t sleep_cycles_seen_ = 0;
};

WatchControl *service = nullptr;

} // namespace

esp_err_t start_watch_control(attadipa::debug::TimeSink *time_sink,
                              attadipa::debug::MeshSink *mesh_sink) {
  if (service != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  const attadipa::platform::BoardProfile *board =
      attadipa::platform::find_board_profile(kBoardProfileId);
  // The queue and the state belong to the physical path, which is built into
  // every image and started first. This transport is a second producer into
  // them, never their owner -- that is the whole of #346.
  attadipa::core::InputQueue *input_queue = physical_input_queue();
  attadipa::core::InputState *input_state = physical_input_state();
  if (board == nullptr || input_queue == nullptr || input_state == nullptr ||
      time_sink == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const std::size_t frame_bytes =
      static_cast<std::size_t>(board->display.width_px) *
      board->display.height_px * 2;
  auto *frame = static_cast<std::uint8_t *>(
      heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frame == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  usb_serial_jtag_driver_config_t usb{};
  usb.rx_buffer_size = 4096;
  usb.tx_buffer_size = kOutputCapacity;
  esp_err_t result = usb_serial_jtag_driver_install(&usb);
  if (result != ESP_OK) {
    heap_caps_free(frame);
    return result;
  }
  WatchControl *candidate = new (std::nothrow)
      WatchControl(*board, *input_queue, *input_state, time_sink, mesh_sink,
                   frame, frame_bytes);
  if (candidate == nullptr) {
    usb_serial_jtag_driver_uninstall();
    heap_caps_free(frame);
    return ESP_ERR_NO_MEM;
  }
  result = candidate->attach();
  if (result != ESP_OK) {
    delete candidate;
    usb_serial_jtag_driver_uninstall();
    heap_caps_free(frame);
    return result;
  }
  usb_serial_jtag_vfs_use_driver();
  service = candidate;
  ESP_LOGI(kTag, "USB watch-control ready; screenshot buffer %u bytes PSRAM",
           static_cast<unsigned>(frame_bytes));
  return ESP_OK;
}
