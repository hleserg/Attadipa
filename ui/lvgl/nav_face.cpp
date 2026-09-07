#include "attadipa/ui/nav_face.h"

#include <algorithm>
#include <cmath>

#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"
#include "generated/attadipa_images.h"

namespace attadipa::ui {
namespace {

// Two places turn centidegrees into radians here — the trail and the north
// marker — and they have to agree to the last digit or the marker drifts off
// the trail's ring.
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
void glowing_dot(lv_obj_t *object, std::int32_t size, lv_color_t colour,
                 lv_opa_t opacity, std::int32_t glow) {
  bare(object);
  lv_obj_set_size(object, size, size);
  lv_obj_set_style_radius(object, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(object, colour, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(object, opacity, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(object, colour, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(object, glow, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(object, opacity, LV_PART_MAIN);
  lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

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
  const lv_color_t warm =
      resolved(ColorRole::AccentPrimary, config.theme, config.pixel_cost);
  const lv_color_t border =
      resolved(ColorRole::BorderSubtle, config.theme, config.pixel_cost);
  // THE TRAIL CHANGES ROLE WITH THE THEME, AND NOT FOR DECORATION. Night is a
  // painted meadow and the trail is the mascot's own glow over it. Day is a
  // near-ivory ground, where that honey is a pale mark on a pale field, so the
  // trail goes back to the navigation teal that the day palette is measured
  // against -- `docs/ui/DESIGN_SYSTEM.md:124` --
  // "Not an opinion and not a review note: WCAG 2.1 relative luminance".
  const lv_color_t trail_colour =
      config.theme == Theme::Night
          ? resolved(ColorRole::AccentGlow, config.theme, config.pixel_cost)
          : accent;

  lv_obj_clean(screen);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, config.width_px, config.height_px);
  lv_obj_set_style_bg_color(screen, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  const bool large = config.width_px >= 320;
  // ONE THING IS BIG AND EVERYTHING ELSE IS SMALL. The face used to set the
  // title and the bearing at 28 and the distance at 64: six rows of nearly the
  // same weight, which is a list rather than a readout. The distance keeps the
  // size it had -- 84 would be wider than `row_width` at "> 1000 km" and a
  // number that clips is a number that lies, which is why it sits below the
  // ring at all -- and the five rows around it drop instead.
  const lv_font_t *display_font =
      large ? &attadipa_nunito_sans_64 : &attadipa_nunito_sans_28;
  const lv_font_t *body_font =
      large ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_14;
  const lv_font_t *small_font =
      large ? &attadipa_nunito_sans_16 : &attadipa_nunito_sans_14;
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
  const std::int32_t ring = shorter * (large ? 40 : 30) / 100;

  // A COLUMN, NOT SIX ANCHORS. The first version of this face anchored the
  // title to the top, the ring below it, and the status and caveat to the
  // bottom. On 410x502 that composed; on 240x240 the two halves met in the
  // middle and the bearing, the status and the caveat were drawn on top of one
  // another. A flex column cannot do that: content that does not fit is
  // clipped at the bottom edge, which is a visible defect rather than an
  // unreadable one, and it fits on both panels as measured below.
  // THE SAME MEADOW THE CLOCK STANDS IN, AND THE SAME ASSET. Two screens of one
  // watch that share nothing visually read as two products; this one is already
  // linked into the binary for the clock, so using it here costs no flash --
  // `ui/assets/generated/attadipa_images.h:39` --
  // "LV_IMAGE_DECLARE(attadipa_background_clock_meadow_night_410x502);".
  // Night only, exactly as the clock gates it: the art is painted for a dark
  // ground and the day palette is measured against ivory, not against it.
  //
  // Created first so that it draws under everything, and outside the layout,
  // because this screen *is* the flex column and a child inside it would be
  // given a row of its own.
  if (config.theme == Theme::Night) {
    background_ = lv_image_create(screen);
    bare(background_);
    lv_image_set_src(background_,
                     &attadipa_background_clock_meadow_night_410x502);
    if (width < 410 || height < 502) {
      // Cover, not fit: the smaller of the two ratios would letterbox, and a
      // letterboxed background is a picture of a screen rather than a screen.
      const unsigned scale =
          std::max(static_cast<unsigned>(width) * 256U / 410U,
                   static_cast<unsigned>(height) * 256U / 502U);
      lv_image_set_scale(background_, scale);
    }
    lv_obj_add_flag(background_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    // `lv_obj_align` centres in the *content* box, and this screen is the flex
    // column, so its top padding is inside that box: centring left a strip of
    // bare page colour along the top edge exactly `pad_top` tall and hung the
    // same amount off the bottom. Half the padding, taken back off the y.
    lv_obj_align(background_, LV_ALIGN_CENTER, 0, -(height / 24) / 2);

    // A SCRIM, BECAUSE THE ART IS BUSIEST WHERE THE SMALLEST TEXT IS. The
    // meadow's grass fills the lower half and the caveat is the quietest row on
    // the screen; over the leaves it was there but not readable. This is the
    // page colour, transparent at the top and about two thirds opaque at the
    // bottom, so the grass still reads as grass and the words sit on something.
    scrim_ = lv_obj_create(screen);
    bare(scrim_);
    lv_obj_add_flag(scrim_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(scrim_, width, height / 2);
    scrim_grad_.dir = LV_GRAD_DIR_VER;
    // TWO STOPS, AND THAT IS THE WHOLE ARRAY. `LV_GRADIENT_MAX_STOPS` is 2 in
    // both configurations — `sim/lv_conf_simulator.h:594` —
    // "#define LV_GRADIENT_MAX_STOPS   2" — and a third assignment is a write
    // one element past `stops[]`, straight into `stops_count` and `dir`. It
    // renders as a bright vertical stripe down the object, which is a much
    // luckier symptom than it deserved.
    scrim_grad_.stops_count = 2;
    scrim_grad_.stops[0] = {page, LV_OPA_TRANSP, 0};
    scrim_grad_.stops[1] = {page, LV_OPA_60, 255};
    lv_obj_set_style_bg_grad(scrim_, &scrim_grad_, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scrim_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(scrim_, LV_ALIGN_BOTTOM_MID, 0, 0);
  }

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
  lv_obj_set_style_text_font(title_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(title_, warm, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(title_, m.px(Dp{3}), LV_PART_MAIN);
  // No opacity on it. Making it recessive by fading it looked right over the
  // meadow and put a measured contrast ratio out of date everywhere else —
  // `docs/ui/DESIGN_SYSTEM.md:124` —
  // "Not an opinion and not a review note: WCAG 2.1 relative luminance", and
  // the ratio in that table is the role at full strength. The size and the
  // letter spacing are what make it quiet.

  // The ring is the frame of reference made visible. It never turns; what moves
  // is the "N", which sits at its top while the face is north-up and at the
  // drawn bearing of north once `point_trail()` has a heading to rotate by.
  ring_ = lv_obj_create(screen);
  bare(ring_);
  lv_obj_set_size(ring_, ring, ring);
  lv_obj_set_style_radius(ring_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ring_, LV_OPA_TRANSP, LV_PART_MAIN);
  // A hairline. It has to be visible enough to read as a frame and quiet
  // enough that the trail inside it is what the eye lands on; at 2dp and 60%
  // over the meadow it was the heaviest thing on the screen.
  lv_obj_set_style_border_width(ring_, m.px(Dp{1}), LV_PART_MAIN);
  lv_obj_set_style_border_color(ring_, border, LV_PART_MAIN);
  lv_obj_set_style_border_opa(ring_, LV_OPA_30, LV_PART_MAIN);

  // THE TRAIL. Seven dots on the large panel and four on the small one: the run
  // is drawn along a fixed fraction of the ring's radius, so on a 240x240 ring
  // that span is about 26 px and seven dots in it touch and become an arc with
  // no head. The dots grow and brighten from tail to head, which is what says
  // which way to walk without saying it in colour.
  const std::int32_t tail_size = m.px(Dp{2});
  const std::int32_t head_size = m.px(Dp{7});
  for (unsigned i = 0; i < kTrailDots; ++i) {
    trail_[i] = lv_obj_create(screen);
    if (i >= trail_dots()) {
      // Built and then hidden rather than not built: `kTrailDots` pointers are
      // cleared together in `clear()`, and a half-filled array is the kind of
      // thing a later edit reads as an accident.
      bare(trail_[i]);
      lv_obj_add_flag(trail_[i], LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_add_flag(trail_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const std::int32_t span = static_cast<std::int32_t>(trail_dots() - 1);
    const std::int32_t step = static_cast<std::int32_t>(i);
    glowing_dot(trail_[i],
                tail_size + (head_size - tail_size) * step / span,
                trail_colour,
                static_cast<lv_opa_t>(LV_OPA_20 +
                                      (LV_OPA_COVER - LV_OPA_20) * step / span),
                m.px(Dp{4}) * step / span);
  }

  // THE MARKER IS CREATED AFTER THE TRAIL, WHICH IS THE WHOLE POINT. LVGL draws
  // siblings in creation order, so this letter is the last thing over the ring
  // and the trail cannot cover it. It carries its own backdrop for the same
  // reason: at a due-north bearing the head of the trail arrives underneath it
  // in both frames — the marker sits at minus the heading and the trail at the
  // bearing minus it, so the two are apart by exactly the true bearing — and a
  // glowing dot behind a small grey letter leaves neither readable.
  //
  // The backdrop is the page colour at half strength, and half is the whole
  // point: the meadow inside the ring is darker than the page, so an opaque
  // page-coloured disc was a grey pill sitting in the dial at every angle. At
  // half it is a soft shadow under the letter on the meadow, nothing at all on
  // the flat day ground, and still enough to take the glow off the head of the
  // trail on the one bearing where the two arrive together.
  north_ = lv_label_create(screen);
  bare(north_);
  lv_label_set_text(north_, text.north);
  lv_obj_set_style_text_font(north_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_color(north_, muted, LV_PART_MAIN);
  lv_obj_set_style_bg_color(north_, page, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(north_, LV_OPA_50, LV_PART_MAIN);
  lv_obj_set_style_radius(north_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_add_flag(north_, LV_OBJ_FLAG_IGNORE_LAYOUT);

  // The wearer, at the centre the trail leaves from. Dim and small: it is the
  // origin of the sentence, not its subject.
  hub_ = lv_obj_create(screen);
  bare(hub_);
  lv_obj_set_size(hub_, m.px(Dp{5}), m.px(Dp{5}));
  lv_obj_set_style_radius(hub_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hub_, ink, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(hub_, LV_OPA_50, LV_PART_MAIN);
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
  lv_obj_set_style_text_font(bearing_, body_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(bearing_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(bearing_, accent, LV_PART_MAIN);
  lv_label_set_long_mode(bearing_, LV_LABEL_LONG_CLIP);

  status_ = lv_label_create(screen);
  bare(status_);
  lv_obj_set_width(status_, row_width);
  lv_obj_set_style_text_font(status_, small_font, LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(status_, m.px(Dp{1}), LV_PART_MAIN);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);

  caveat_ = lv_label_create(screen);
  bare(caveat_);
  lv_obj_set_width(caveat_, row_width);
  lv_obj_set_style_text_font(caveat_, small_font, LV_PART_MAIN);
  lv_obj_set_style_text_align(caveat_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_color(caveat_, muted, LV_PART_MAIN);
  lv_label_set_long_mode(caveat_, LV_LABEL_LONG_WRAP);

  built_ = true;
  update(text);
}

void NavFace::point_trail(const apps::NavText &text) {
  if (trail_[0] == nullptr || ring_ == nullptr) {
    return;
  }
  // The flex column places the ring; these sit on top of it and are excluded
  // from that layout, so their coordinates have to be read back after it has
  // run rather than assumed.
  lv_obj_update_layout(screen_);
  const std::int32_t centre_x = lv_obj_get_x(ring_) + lv_obj_get_width(ring_) / 2;
  const std::int32_t centre_y = lv_obj_get_y(ring_) + lv_obj_get_height(ring_) / 2;
  const double radius = lv_obj_get_width(ring_) / 2.0;

  // North-up: the marker sits inside the ring's top edge, not above it — above
  // it, it collided with the title on the 240x240 panel, where every row is
  // close to its neighbour. Head-up: the ring has turned with the wrist, so the
  // marker travels to where north actually is. It has to move. A marker pinned
  // to the top beside a trail that turns with the wrist makes the ring and the
  // trail two answers to one question, and nothing on the screen says which to
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
    // No bearing, no trail. A trail parked at a default is the same lie as
    // printing `000°`, drawn instead of written.
    for (unsigned i = 0; i < kTrailDots; ++i) {
      lv_obj_add_flag(trail_[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(hub_, LV_OBJ_FLAG_HIDDEN);
    trail_drawn_ = false;
    return;
  }
  for (unsigned i = 0; i < trail_dots(); ++i) {
    lv_obj_remove_flag(trail_[i], LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(hub_, LV_OBJ_FLAG_HIDDEN);

  // The wrist-relative angle when the watch knows which way it is turned, and
  // the true bearing otherwise. One variable, so that the cache below and the
  // trigonometry under it can never be told different things.
  const std::uint16_t drawn_centideg =
      text.has_arrow ? text.arrow_centideg : text.bearing_centideg;
  // Moving seven objects invalidates seven areas, and the readout re-formats
  // every tick whether or not the bearing moved. The ring, though, is placed by
  // the flex column and may have moved under a relaid-out row above it, so the
  // cache is checked only after the ring-following objects are placed.
  if (trail_drawn_ && trail_centideg_ == drawn_centideg) {
    return;
  }

  // Screen y grows downward and a bearing grows clockwise from north, so the
  // sine goes on x and the *negated* cosine on y. Getting this pair the wrong
  // way round produces a trail that is plausible everywhere and correct only at
  // the four cardinal points.
  const double radians = drawn_centideg * kPi / 18000.0;
  // The head stops short of the rim so that a due-north bearing does not put it
  // over the "N" — which it would in either mode, the marker sitting at minus
  // the heading and the trail at the bearing minus it. That marker is the only
  // thing on screen saying which frame the face is in, so nothing may sit on
  // it. The needle cleared the marker by stopping at 0.62 of the radius; a dot
  // is wider than a line was, and at `--nav-state far`, whose bearing is due
  // north, the head covered the "С" outright — on 240x240 it erased it. Pulling
  // the head in far enough to clear the marker's box costs every other angle a
  // stub of a trail, because the obstacle is one small box at one angle and not
  // an annulus. So the marker is drawn over the trail instead, on a backdrop of
  // its own; see `build()`. These two fractions are the needle's, kept because
  // they are what was composed against both panels.
  const double head = radius * 0.66;
  const double tail = radius * 0.24;
  const double span = static_cast<double>(trail_dots() - 1);
  for (unsigned i = 0; i < trail_dots(); ++i) {
    const double along = tail + (head - tail) * static_cast<double>(i) / span;
    lv_obj_set_pos(
        trail_[i],
        static_cast<std::int32_t>(centre_x + std::sin(radians) * along -
                                  lv_obj_get_width(trail_[i]) / 2.0),
        static_cast<std::int32_t>(centre_y - std::cos(radians) * along -
                                  lv_obj_get_height(trail_[i]) / 2.0));
  }
  trail_drawn_ = true;
  trail_centideg_ = drawn_centideg;
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
  point_trail(text);
}

void NavFace::clear() {
  if (screen_ != nullptr && built_) {
    lv_obj_clean(screen_);
  }
  screen_ = nullptr;
  background_ = nullptr;
  scrim_ = nullptr;
  title_ = nullptr;
  ring_ = nullptr;
  north_ = nullptr;
  for (lv_obj_t *&dot : trail_) {
    dot = nullptr;
  }
  hub_ = nullptr;
  distance_ = nullptr;
  bearing_ = nullptr;
  status_ = nullptr;
  caveat_ = nullptr;
  built_ = false;
  trail_drawn_ = false;
}

} // namespace attadipa::ui
