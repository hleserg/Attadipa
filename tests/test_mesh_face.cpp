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
#include <cstring>
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

// How many times the panel has been handed pixels. LVGL calls this once per
// rendered area and not at all when nothing was invalidated, which is what
// makes it the evidence for "the face did not repaint" -- an assertion about
// the frame buffer could not tell a screen that was redrawn identically from
// one that was left alone, and it is the redrawing that costs.
int g_flushes = 0;

void flush_cb(lv_display_t *display, const lv_area_t *, std::uint8_t *) {
  ++g_flushes;
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

// THE SCREEN A FACE IS GIVEN BELONGS TO THAT FACE.
//
// Page turning hands every face the same `lv_screen_active()` and deletes
// nothing, so arriving at the mesh screen from the entry screen left the
// provisioning keypad still parented to it: invisible under the new paint,
// still `LV_OBJ_FLAG_CLICKABLE`, and swallowing the tap that turns the page.
// `MeshFace::clear()` cleans the screen the face is holding, which on a first
// build is none at all, so it never reached this.
//
// The count is taken from a build onto an empty screen rather than written
// down, because the number is the layout's business and this is not a test of
// how many widgets the layout has.
void a_build_owns_the_screen_it_is_given(const platform::BoardProfile &board) {
  lv_display_t *display = open_panel(board);
  lv_obj_t *screen = lv_screen_active();
  const apps::MeshText text = apps::format_mesh(linked(), l10n::Locale::En);

  ui::MeshFace alone;
  alone.build(screen, config_for(board), text);
  const std::uint32_t mine = lv_obj_get_child_count(screen);
  alone.clear();
  CHECK(mine > 0);

  lv_obj_t *leftover = lv_obj_create(screen);
  lv_obj_add_flag(leftover, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_create(screen);
  CHECK(lv_obj_get_child_count(screen) == 2);

  ui::MeshFace face;
  face.build(screen, config_for(board), text);
  lv_refr_now(display);
  check(lv_obj_get_child_count(screen) == mine,
        "the face is the only thing left on the screen it was built onto",
        __LINE__);

  face.clear();
}

// The first row that belongs to neither the message nor the sender line under
// it. On the tall panel the measurements start at y=452 and the meta row sits
// between; 240 px draws no meta row at all and spends its last rows on the
// measurements. Taking the band from below the meta row rather than above it is
// what makes this a guard on *both* air-fed rows: either one growing by a
// single line lands inside it.
std::uint32_t below_message(bool big) { return big ? 452 : 192; }

// Text off the air is ellipsised on its own row, not run through what is under it.
//
// `LV_LABEL_LONG_DOT` puts the dots in only where the height is FIXED, and the
// height this label was first given was content -- so a message longer than the
// fixture's grew downwards over the measurement row and the sender line, and a
// screenshot was the only thing that caught it. Nothing below the message's own
// line may depend on how long the message is, and that is what this asserts:
// two frames differing in the message alone, compared from the first row the
// message does not own.
void a_long_message_stays_on_its_line(const platform::BoardProfile &board) {
  const bool big = board.display.width_px >= 320;
  const std::uint32_t w = board.display.width_px;
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;

  core::MeshStatus status = linked();
  // A sender to start from, so the meta row exists in both frames and the
  // comparison is about its *length* rather than about it appearing.
  const char *const kShortName = "Ridge";
  std::memcpy(status.last_sender.data(), kShortName, std::strlen(kShortName));
  face.build(lv_screen_active(), config_for(board),
             apps::format_mesh(status, l10n::Locale::En));
  lv_refr_now(display);
  const std::vector<std::uint8_t> before = *g_frame;

  // BOTH FIELDS AT ONCE, BECAUSE BOTH COME OFF THE LINK AND EITHER CAN GROW.
  //
  // `MeshText::message` carries the whole of what arrived -- the buffer no
  // longer truncates -- and `sender` is a peer's advertised name, up to
  // `kMeshPeerNameBytes`, which the meta row joins with the delivery word. The
  // message row was fixed first and the row under it had the identical defect;
  // filling only one of them would have left the other's growth uncaught, which
  // is exactly how the second one survived the first fix.
  std::memset(status.last_message.data(), 'M', status.last_message.size() - 1);
  status.last_message[status.last_message.size() - 1] = '\0';
  std::memset(status.last_sender.data(), 'S', status.last_sender.size() - 1);
  status.last_sender[status.last_sender.size() - 1] = '\0';
  face.update(apps::format_mesh(status, l10n::Locale::En));
  lv_refr_now(display);

  const std::size_t from =
      static_cast<std::size_t>(below_message(big)) * w * 2;
  int moved = 0;
  for (std::size_t at = from; at < before.size(); ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++moved;
    }
  }
  check(moved == 0, "a full-length message moves nothing under its line",
        __LINE__);
  if (moved != 0) {
    std::fprintf(stderr, "  %ux%u: %d bytes changed below y=%u\n", w,
                 board.display.height_px, moved, below_message(big));
  }

  // And the counter-check: the message row itself DID change, or the comparison
  // above was made between two identical frames and proves nothing.
  int drew = 0;
  for (std::size_t at = 0; at < from; ++at) {
    if (before[at] != (*g_frame)[at]) {
      ++drew;
    }
  }
  check(drew > 0, "the longer message is drawn somewhere", __LINE__);

  face.clear();
}

// The same readout twice does not reach the panel.
//
// `refresh_mesh()` calls `update()` at 2 Hz with a struct that changes only
// when something on the mesh does, and the face laid out and repainted forty
// widgets for every one of those ticks. The counter-check is not optional: a
// face that had stopped drawing altogether would pass the first assertion.
void an_unchanged_readout_is_not_redrawn(const platform::BoardProfile &board) {
  lv_display_t *display = open_panel(board);
  ui::MeshFace face;
  const apps::MeshText text = apps::format_mesh(linked(), l10n::Locale::En);
  face.build(lv_screen_active(), config_for(board), text);
  lv_refr_now(display);

  g_flushes = 0;
  face.update(text);
  lv_refr_now(display);
  check(g_flushes == 0, "an identical readout does not repaint the panel",
        __LINE__);

  // A field that is drawn, and a value that is not a fixture's: the signal to
  // noise ratio moves on its own between two readouts that are otherwise the
  // same, which is the change this guard has to let through.
  apps::MeshText moved = text;
  std::snprintf(moved.snr, sizeof(moved.snr), "9.75");
  face.update(moved);
  lv_refr_now(display);
  check(g_flushes > 0, "a changed readout does repaint the panel", __LINE__);

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
    a_long_message_stays_on_its_line(*board);
    a_build_owns_the_screen_it_is_given(*board);
    an_unchanged_readout_is_not_redrawn(*board);
  }

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("mesh face: all checks passed on both panels\n");
  return 0;
}
