// The mesh face, drawn, on both panels, and then looked at by counting pixels.
//
// The screen this replaces could not be rendered anywhere: it was built inside
// `firmware/main/waveshare_board.cpp` from offsets measured for one display, so
// there was no test that could put it on a surface and no screenshot anybody
// could open. That is the regression this file exists to make impossible again.
//
// The assertion that matters is the honest-state one, and it is made against
// the rendered frame rather than against the struct that fed it: a watch with
// no node named draws NO LINK. Not a dim link, not a socket waiting, not a row
// of zeroes -- nothing to the left or the right of the lone watch. `test_mesh`
// checks that `apps::format_mesh()` says so; this checks that the pixels agree,
// which is a different claim and the one a wearer sees.
//
// Nothing here has touched a panel. **NOT EXECUTED -- HARDWARE REQUIRED** for
// anything about a physical display.

#include <cstdio>
#include <cstdint>
#include <iterator>
#include <vector>

#include "lvgl.h"

#include "attadipa/apps/mesh.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/ui/mesh_face.h"

using namespace attadipa;

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::vector<std::vector<std::uint8_t>> g_buffers;
std::vector<std::uint8_t> *g_frame = nullptr;

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  lv_display_flush_ready(display);
}

// A display with no window behind it. The buffer is the frame: LVGL renders the
// whole panel into it, so what is in it after `lv_refr_now` is what the wearer
// would be looking at.
lv_display_t *open_panel(const platform::BoardProfile &board) {
  const std::uint32_t width = board.display.width_px;
  const std::uint32_t height = board.display.height_px;
  g_buffers.emplace_back(static_cast<std::size_t>(width) * height * 2, 0);
  g_frame = &g_buffers.back();

  lv_display_t *display = lv_display_create(static_cast<std::int32_t>(width),
                                            static_cast<std::int32_t>(height));
  lv_display_set_default(display);
  lv_display_set_dpi(display, board.display.dpi());
  lv_display_set_buffers(display, g_frame->data(), nullptr,
                         static_cast<std::uint32_t>(g_frame->size()),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush_cb);
  return display;
}

std::uint16_t pixel(std::uint32_t x, std::uint32_t y, std::uint32_t width) {
  const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 2;
  return static_cast<std::uint16_t>((*g_frame)[at] |
                                    ((*g_frame)[at + 1] << 8U));
}

// How many pixels in a band differ from the page.
//
// The reference is taken from the top-right corner rather than from the token
// table: the face composites, and a colour computed here would be asserting
// what the conversion ought to produce instead of what it did. The title is the
// only thing near the top and it is at the top *left*, so the corner is page in
// every state.
int painted(std::uint32_t x0, std::uint32_t x1, std::uint32_t y0,
            std::uint32_t y1, std::uint32_t width) {
  const std::uint16_t page = pixel(width - 3, 3, width);
  int count = 0;
  for (std::uint32_t y = y0; y <= y1; ++y) {
    for (std::uint32_t x = x0; x <= x1; ++x) {
      if (pixel(x, y, width) != page) {
        ++count;
      }
    }
  }
  return count;
}

core::MeshStatus unprovisioned() {
  core::MeshStatus status;
  status.availability = core::Availability::Unprovisioned;
  status.transport = core::TransportPhase::Absent;
  return status;
}

core::MeshStatus linked() {
  core::MeshStatus status;
  status.availability = core::Availability::Ready;
  status.transport = core::TransportPhase::Ready;
  status.node_id.public_key[0] = 0x4C;
  status.node_id.public_key[1] = 0x9A;
  status.node_id.public_key[2] = 0x2F;
  status.node_id.public_key[3] = 0x7B;
  status.has_node_id = true;
  std::snprintf(status.node_name.data(), status.node_name.size(), "Ridge");
  std::snprintf(status.last_message.data(), status.last_message.size(), "here");
  status.snr_quarter_db = 29;
  status.has_snr = true;
  status.peers_reported = 3;
  status.mtu = 244;
  return status;
}

ui::MeshFaceConfig config_for(const platform::BoardProfile &board) {
  return {board.display.width_px, board.display.height_px, ui::Theme::Night,
          board.display.technology == platform::PanelTechnology::Amoled
              ? ui::PixelCost::PerPixel
              : ui::PixelCost::Fixed,
          ui::Metrics::for_dpi(board.display.dpi())};
}

// The band the link glyph occupies, and how far from the centre a pixel has to
// be before only the link could have put it there. The lone watch is 0.9r wide
// either side of the centre, and these clear it on both panels.
//
// `y0` starts below the title, which is the one other thing drawn this high and
// is at the top *left*. The first version of this band began at y=26 and
// counted the M of MESH as a link, which is the sort of failure that would have
// been read as a face bug had the run not printed which side it was on.
struct Band {
  std::uint32_t clearance;
  std::uint32_t y0;
  std::uint32_t y1;
};

Band band_for(bool big) {
  return big ? Band{70, 60, 230} : Band{45, 40, 118};
}

void an_unnamed_node_draws_no_link(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(unprovisioned(), l10n::Locale::En));
  CHECK(face.built());
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const Band b = band_for(big);
  const std::uint32_t cx = w / 2;

  // Both sides of the watch, because half a missing link is still a link.
  const int right = painted(cx + b.clearance, w - 1, b.y0, b.y1, w);
  const int left = painted(1, cx - b.clearance, b.y0, b.y1, w);
  check(right == 0, "no link is drawn to the right of a watch with no node",
        __LINE__);
  check(left == 0, "no link is drawn to the left of a watch with no node",
        __LINE__);
  if (right != 0 || left != 0) {
    std::fprintf(stderr, "  %ux%u: %d right, %d left\n", w,
                 board.display.height_px, right, left);
  }

  face.clear();
}

// The counter-check, and it is not optional: a band that is empty because the
// face drew nothing at all would pass the test above. A linked watch must paint
// in exactly the place the unnamed one left blank.
void a_linked_node_draws_one(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(linked(), l10n::Locale::En));
  CHECK(face.built());
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const Band b = band_for(big);
  const int right = painted(w / 2 + b.clearance, w - 1, b.y0, b.y1, w);
  check(right > 0, "a linked watch paints where an unnamed one does not",
        __LINE__);

  face.clear();
}

// A layout written for the other panel.
//
// The first version of this checked that the bottom row was unpainted and that
// something was drawn above it, and a mutation giving the 240x240 board the
// 410x502 offsets PASSED IT: almost everything landed off the display, and the
// one surviving line -- the state word -- happened to fall in the strip the
// test looked at. An absence cannot be detected by asking whether anything at
// all is there.
//
// So this names every strip the layout puts something in, and requires all of
// them. A composition that fits uses the whole height; one written for a taller
// panel leaves holes where its rows fell off the end, and it is the holes that
// are the evidence.
struct Strip {
  std::uint32_t y0;
  std::uint32_t y1;
  const char *what;
};

void the_layout_uses_the_whole_panel(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(linked(), l10n::Locale::En));
  lv_refr_now(display);

  const std::uint32_t w = board.display.width_px;
  const std::uint32_t h = board.display.height_px;

  static const Strip kBig[] = {
      {88, 168, "the link glyph"},   {198, 224, "the state word"},
      {286, 330, "the node key"},    {370, 418, "the message"},
      {454, 496, "the measurements"}};
  static const Strip kSmall[] = {
      {42, 76, "the link glyph"},    {88, 106, "the state word"},
      {116, 130, "the node key"},    {152, 186, "the message"},
      {196, 228, "the measurements"}};

  const Strip *strips = big ? kBig : kSmall;
  const std::size_t count = big ? std::size(kBig) : std::size(kSmall);
  for (std::size_t i = 0; i < count; ++i) {
    const int painted_here = painted(1, w - 2, strips[i].y0, strips[i].y1, w);
    if (painted_here == 0) {
      std::fprintf(stderr, "FAIL line %d: %ux%u draws nothing where %s goes\n",
                   __LINE__, w, h, strips[i].what);
      ++failures;
    }
  }

  // And nothing runs off the end of it.
  check(painted(1, w - 2, h - 2, h - 1, w) == 0,
        "the bottom row of the panel is not painted", __LINE__);

  face.clear();
}

} // namespace

int main() {
  lv_init();

  const char *kBoards[] = {"waveshare-amoled-206", "t-watch-s3-plus"};
  for (const char *id : kBoards) {
    const platform::BoardProfile *board = platform::find_board_profile(id);
    if (board == nullptr) {
      std::fprintf(stderr, "FAIL: no board profile '%s'\n", id);
      ++failures;
      continue;
    }
    an_unnamed_node_draws_no_link(*board);
    a_linked_node_draws_one(*board);
    the_layout_uses_the_whole_panel(*board);
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("mesh face: all checks passed on both panels\n");
  return 0;
}
