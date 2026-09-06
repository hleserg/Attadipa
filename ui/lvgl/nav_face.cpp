#include "attadipa/ui/nav_face.h"

#include <cmath>

#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"

namespace attadipa::ui {
namespace {

// Two places turn centidegrees into radians here — the needle and the north
// marker — and they have to agree to the last digit or the marker drifts off
// the needle's ring.
constexpr double kPi = 3.14159265358979323846;

lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost) {
  const auto value = color(role, theme, pixel_cost);
  return value ? lv_color_hex(value->packed()) : lv_color_black();
}

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
}

// Whether the status is one the reader has to act on. `Ready` is not, and
// neither is a stale node coordinate that still has a usable answer beside it —
// those are ordinary, and colouring them as alarms teaches people to ignore the
// colour.
bool is_alarm(apps::NavStatus status) {
  return status != apps::NavStatus::Ready &&
         status != apps::NavStatus::NodePositionStale;
}

} // namespace

void NavFace::build(lv_obj_t *screen, const NavFaceConfig &config,
                    const apps::NavText &text) {
  clear();
  screen_ = screen;
  config_ = config;

  const Metrics &m = config.metrics;
  const lv_color_t page =
      resolved(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost);
  const lv_color_t ink =
      resolved(ColorRole::TextPrimary, config.theme, config.pixel_cost);
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config.theme, config.pixel_cost);
  const lv_color_t accent =
      resolved(ColorRole::Navigation, config.theme, config.pixel_cost);
  const lv_color_t border =
      resolved(ColorRole::BorderSubtle, config.theme, config.pixel_cost);

  lv_obj_clean(screen);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, config.width_px, config.height_px);
  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  const bool large = config.width_px >= 320;
  const lv_font_t *display_font =
      large ? &attadipa_nunito_sans_64 : &attadipa_nunito_sans_28;
  const lv_font_t *title_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_16;
  const lv_font_t *bearing_font =
      large ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_16;
  const lv_font_t *body_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_14;
  lv_obj_set_style_text_font(screen, body_font, LV_PART_MAIN);

  // THE LAYOUT IS PROPORTIONAL TO THE PANEL, NOT IN DENSITY-INDEPENDENT PIXELS.
  //
  // `Metrics::px()` scales a `Dp` by the panel's density, and on the 410x502
  // AMOLED that multiplies by about 1.9 — so a ring sized in `Dp` came out
  // 290 px across on a 410 px panel and pushed the distance off the bottom
  // edge. Dp is the right unit for a thing that must stay the same *physical*
  // size, which is what a border and a letter gap are; a compass ring is a
  // composition, and a composition is a fraction of the panel it sits on.
  const std::int32_t width = static_cast<std::int32_t>(config.width_px);
  const std::int32_t height = static_cast<std::int32_t>(config.height_px);
  const std::int32_t shorter = width < height ? width : height;
  const std::int32_t margin = width / 16;
  const std::int32_t row_width = width - margin * 2;
  const std::int32_t ring = shorter * (large ? 44 : 30) / 100;

  // A COLUMN, NOT SIX ANCHORS. The first version of this face anchored the
  // title to the top, the ring below it, and the status and caveat to the
  // bottom. On 410x502 that composed; on 240x240 the two halves met in the
  // middle and the bearing, the status and the caveat were drawn on top of one
  // another. A flex column cannot do that: content that does not fit is
  // clipped at the bottom edge, which is a visible defect rather than an
  // unreadable one, and it fits on both panels as measured below.
  lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(screen, height / 24, LV_PART_MAIN);
  lv_obj_set_style_pad_row(screen, height / 100, LV_PART_MAIN);
  lv_obj_set_style_pad_left(screen, margin, LV_PART_MAIN);
  lv_obj_set_style_pad_right(screen, margin, LV_PART_MAIN);

  title_ = lv_label_create(screen);
  bare(title_);
  lv_label_set_text(title_, text.title);
  lv_obj_set_style_text_font(title_, title_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(title_, accent, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(title_, m.px(Dp{2}), LV_PART_MAIN);

  // The ring is the frame of reference made visible. It never turns; what moves
  // is the "N", which sits at its top while the face is north-up and at the
  // drawn bearing of north once `point_needle()` has a heading to rotate by.
  ring_ = lv_obj_create(screen);
  bare(ring_);
  lv_obj_set_size(ring_, ring, ring);
  lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ring_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ring_, m.px(Dp{2}), LV_PART_MAIN);
  lv_obj_set_style_border_color(ring_, border, LV_PART_MAIN);
  lv_obj_set_style_border_opa(ring_, LV_OPA_60, LV_PART_MAIN);

  north_ = lv_label_create(screen);
  bare(north_);
  lv_label_set_text(north_, text.north);
  lv_obj_set_style_text_font(north_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(north_, muted, LV_PART_MAIN);
  lv_obj_add_flag(north_, LV_OBJ_FLAG_IGNORE_LAYOUT);

  needle_ = lv_line_create(screen);
  bare(needle_);
  lv_obj_add_flag(needle_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  // Sized to the ring in point_needle(), not to the panel: an lv_line
  // invalidates its own area, and a needle the size of the screen makes every
  // bearing change a full-panel flush.
  lv_obj_set_style_line_width(needle_, m.px(Dp{6}), LV_PART_MAIN);
  lv_obj_set_style_line_color(needle_, accent, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(needle_, true, LV_PART_MAIN);

  hub_ = lv_obj_create(screen);
  bare(hub_);
  lv_obj_set_size(hub_, m.px(Dp{10}), m.px(Dp{10}));
  lv_obj_set_style_radius(hub_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hub_, accent, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hub_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_flag(hub_, LV_OBJ_FLAG_IGNORE_LAYOUT);

  // The distance goes below the ring rather than inside it. "> 1000 km" at
  // the display size is wider than any ring that still fits the panel, and a
  // number that clips is a number that lies.
  distance_ = lv_label_create(screen);
  bare(distance_);
  lv_obj_set_width(distance_, row_width);
  lv_obj_set_style_text_font(distance_, display_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(distance_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(distance_, ink, LV_PART_MAIN);
  lv_label_set_long_mode(distance_, LV_LABEL_LONG_CLIP);

  bearing_ = lv_label_create(screen);
  bare(bearing_);
  lv_obj_set_width(bearing_, row_width);
  lv_obj_set_style_text_font(bearing_, bearing_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(bearing_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(bearing_, accent, LV_PART_MAIN);
  lv_label_set_long_mode(bearing_, LV_LABEL_LONG_CLIP);

  status_ = lv_label_create(screen);
  bare(status_);
  lv_obj_set_width(status_, row_width);
  lv_obj_set_style_text_font(status_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);

  caveat_ = lv_label_create(screen);
  bare(caveat_);
  lv_obj_set_width(caveat_, row_width);
  lv_obj_set_style_text_font(caveat_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(caveat_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(caveat_, muted, LV_PART_MAIN);
  lv_label_set_long_mode(caveat_, LV_LABEL_LONG_WRAP);

  built_ = true;
  update(text);
}

void NavFace::point_needle(const apps::NavText &text) {
  if (needle_ == nullptr || ring_ == nullptr) {
    return;
  }
  // The flex column places the ring; these three sit on top of it and are
  // excluded from that layout, so their coordinates have to be read back after
  // it has run rather than assumed.
  lv_obj_update_layout(screen_);
  const std::int32_t centre_x = lv_obj_get_x(ring_) + lv_obj_get_width(ring_) / 2;
  const std::int32_t centre_y = lv_obj_get_y(ring_) + lv_obj_get_height(ring_) / 2;
  const double radius = lv_obj_get_width(ring_) / 2.0;

  // North-up: the marker sits inside the ring's top edge, not above it — above
  // it, it collided with the title on the 240x240 panel, where every row is
  // close to its neighbour. Head-up: the ring has turned with the wrist, so the
  // marker travels to where north actually is. It has to move. A marker pinned
  // to the top beside a needle that turns with the wrist makes the ring and the
  // needle two answers to one question, and nothing on the screen says which to
  // follow.
  //
  // THE NORTH-UP BRANCH IS THE EXPRESSION THIS USED TO BE, CHARACTER FOR
  // CHARACTER, and that is the whole reason there are two branches. The rotated
  // form below is real arithmetic that passes through the same point — but only
  // in real arithmetic. `centre_y` and the two half-extents are integer
  // divisions and the rotated form truncates a `double` instead, so on an odd
  // ring height or an odd label width the two disagree by a pixel. Writing the
  // old expression out is a guarantee; a screenshot comparison would only ever
  // have been a measurement of today's two label widths.
  if (!text.has_arrow) {
    lv_obj_set_pos(north_, centre_x - lv_obj_get_width(north_) / 2,
                   lv_obj_get_y(ring_) + lv_obj_get_height(ring_) / 12);
  } else {
    // Its centre rides five twelfths of the ring's height out from the ring's
    // centre, which is the same inset the branch above sets.
    const double marker_orbit = lv_obj_get_height(ring_) * 5.0 / 12.0 -
                                lv_obj_get_height(north_) / 2.0;
    // The negation is on a `double`, not on the centidegrees: `%` promotes them
    // to `unsigned`, where unary minus is a wrap to about 4.29 billion rather
    // than a turn anticlockwise.
    const double marker_radians =
        -static_cast<double>(text.heading_centideg % 36000U) * kPi / 18000.0;
    lv_obj_set_pos(
        north_,
        static_cast<std::int32_t>(centre_x + std::sin(marker_radians) * marker_orbit -
                                  lv_obj_get_width(north_) / 2.0),
        static_cast<std::int32_t>(centre_y - std::cos(marker_radians) * marker_orbit -
                                  lv_obj_get_height(north_) / 2.0));
  }
  lv_obj_set_pos(hub_, centre_x - lv_obj_get_width(hub_) / 2,
                 centre_y - lv_obj_get_height(hub_) / 2);

  if (!text.has_bearing) {
    // No bearing, no needle. A needle parked at a default is the same lie as
    // printing `000°`, drawn instead of written.
    lv_obj_add_flag(needle_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(hub_, LV_OBJ_FLAG_HIDDEN);
    needle_drawn_ = false;
    return;
  }
  lv_obj_remove_flag(needle_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(hub_, LV_OBJ_FLAG_HIDDEN);
  // The needle *is* the ring's box, so it follows the ring whether or not the
  // bearing moved. Above the cache check, not below it, so the two can never
  // drift apart; LVGL no-ops an unchanged size or position, so it costs
  // nothing on the ticks that change neither.
  lv_obj_set_size(needle_, lv_obj_get_width(ring_), lv_obj_get_height(ring_));
  lv_obj_set_pos(needle_, lv_obj_get_x(ring_), lv_obj_get_y(ring_));

  // Re-pointing the line invalidates the whole object, and the readout
  // re-formats every tick whether or not the bearing moved.
  // The wrist-relative angle when the watch knows which way it is turned, and
  // the true bearing otherwise. One variable, so that the cache below and the
  // trigonometry under it can never be told different things.
  const std::uint16_t drawn_centideg =
      text.has_arrow ? text.arrow_centideg : text.bearing_centideg;
  if (needle_drawn_ && needle_centideg_ == drawn_centideg) {
    return;
  }

  // Screen y grows downward and a bearing grows clockwise from north, so the
  // sine goes on x and the *negated* cosine on y. Getting this pair the wrong
  // way round produces a needle that is plausible everywhere and correct only
  // at the four cardinal points.
  const double radians = drawn_centideg * kPi / 18000.0;
  // The tip stops short of the rim so that a due-north bearing does not put the
  // needle over the "N" — which it would in either mode, the marker sitting at
  // minus the heading and the needle at the bearing minus it. That marker is
  // the only thing on screen saying which frame the face is in.
  const double tip = radius * 0.62;
  const double tail = radius * 0.24;

  // Points are relative to the needle object, and the object is the ring, so
  // the ring's origin comes off each of them.
  const std::int32_t local_x = lv_obj_get_width(ring_) / 2;
  const std::int32_t local_y = lv_obj_get_height(ring_) / 2;
  needle_points_[0] = {
      static_cast<lv_value_precise_t>(local_x - std::sin(radians) * tail),
      static_cast<lv_value_precise_t>(local_y + std::cos(radians) * tail)};
  needle_points_[1] = {static_cast<lv_value_precise_t>(local_x),
                       static_cast<lv_value_precise_t>(local_y)};
  needle_points_[2] = {
      static_cast<lv_value_precise_t>(local_x + std::sin(radians) * tip),
      static_cast<lv_value_precise_t>(local_y - std::cos(radians) * tip)};
  lv_line_set_points(needle_, needle_points_, 3);
  needle_drawn_ = true;
  needle_centideg_ = drawn_centideg;
}

void NavFace::update(const apps::NavText &text) {
  if (!built_) {
    return;
  }
  // Both re-read on every update: a locale change is a new NavText, not a
  // rebuild, and a title left from the previous language is the bug that
  // pattern exists to avoid.
  lv_label_set_text(title_, text.title);
  lv_label_set_text(north_, text.north);
  lv_label_set_text(distance_, text.distance);
  if (text.cardinal[0] != '\0') {
    lv_label_set_text_fmt(bearing_, "%s %s", text.bearing, text.cardinal);
  } else {
    lv_label_set_text(bearing_, text.bearing);
  }
  lv_label_set_text(status_, text.status);
  lv_label_set_text(caveat_, text.caveat);
  lv_obj_set_style_text_color(
      status_,
      resolved(is_alarm(text.status_code) ? ColorRole::Warning
                                          : ColorRole::TextMuted,
               config_.theme, config_.pixel_cost),
      LV_PART_MAIN);
  point_needle(text);
}

void NavFace::clear() {
  if (screen_ != nullptr && built_) {
    lv_obj_clean(screen_);
  }
  screen_ = nullptr;
  title_ = nullptr;
  ring_ = nullptr;
  north_ = nullptr;
  needle_ = nullptr;
  hub_ = nullptr;
  distance_ = nullptr;
  bearing_ = nullptr;
  status_ = nullptr;
  caveat_ = nullptr;
  built_ = false;
  needle_drawn_ = false;
}

} // namespace attadipa::ui
