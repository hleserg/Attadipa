#include "attadipa/ui/mesh_face.h"

#include <algorithm>

#include <cstring>

#include "attadipa/ui/tokens.h"
#include "attadipa_fonts.h"

namespace attadipa::ui {
namespace {

lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost) {
  const auto value = color(role, theme, pixel_cost);
  return value ? lv_color_hex(value->packed()) : lv_color_black();
}

void bare(lv_obj_t *object) {
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(object, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t colour) {
  lv_obj_t *object = lv_label_create(parent);
  bare(object);
  lv_obj_set_style_text_font(object, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(object, colour, LV_PART_MAIN);
  return object;
}

// Empty means "not there", so it means "not drawn". This is the whole of the
// honest-state rule as it reaches the pixels: `apps::MeshText` leaves a field
// empty rather than inventing a placeholder, and every field goes through here.
void show(lv_obj_t *object, const char *text) {
  if (text == nullptr || text[0] == '\0') {
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(object, text);
}

void hide(lv_obj_t *object) { lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN); }

// The two lengths this face draws that are not positions: the weight of an
// outline, and the tracking under a small capitalised word.
//
// Both are pixel counts, and a pixel is a different physical size on each
// panel -- 314 dpi against 220. Writing `big ? 3 : 2` compensates for that by
// hand and gets it about right, but it is the arithmetic `Metrics` exists to
// do, and `tools/ui/check_raw_values.py` cannot see inside a ternary to say so.
// Integer `Dp` has no way to write 1.5 dp, so the halving is done in pixels
// after the scaling rather than before it -- `m.px()` first, then `/ 2`.
// This used to cite the same shape in `provision_face.cpp`; that call went
// with the entry screen's rewrite in #469, so the reason is written here
// instead of pointed at.
std::int32_t stroke(const Metrics &m) { return m.px(Dp{3}) / 2; }

std::int32_t tracking_wide(const Metrics &m) { return m.px(dp_of(Space::Xs)) / 2; }

std::int32_t tracking_tight(const Metrics &m) { return m.px(dp_of(Space::Xs)) / 4; }

// The colour a state is spoken in.
//
// `Danger` is `std::nullopt` in every column of the token table -- there is no
// red in either owner palette and inventing one is a visual-identity decision
// that is not this face's to take (`ui/src/color.cpp:67` —
// "    {ColorRole::Danger, ColorKind::Foreground, std::nullopt, std::nullopt, std::nullopt},").
// So a severed link is Warning, which is the strongest thing the palette says.
ColorRole role_for(apps::MeshLink link) {
  switch (link) {
  case apps::MeshLink::Linked:
    return ColorRole::Success;
  case apps::MeshLink::Reaching:
    return ColorRole::AccentPrimary;
  case apps::MeshLink::Broken:
  case apps::MeshLink::TurnedAway:
    return ColorRole::Warning;
  case apps::MeshLink::NoNode:
  case apps::MeshLink::NoRadio:
  case apps::MeshLink::Silent:
  case apps::MeshLink::Resting:
    return ColorRole::TextMuted;
  }
  return ColorRole::TextMuted;
}

} // namespace

void MeshFace::build(lv_obj_t *screen, const MeshFaceConfig &config,
                     const apps::MeshText &text) {
  clear();
  screen_ = screen;
  config_ = config;

  // Whatever was on this screen is not part of this face.
  //
  // `clear()` above cleans the screen this face was holding, which on a first
  // build is none, and page turning hands every face the same
  // `lv_screen_active()` without deleting anything on it -- so arriving here
  // from the entry screen left the provisioning keypad parented underneath,
  // invisible under the new paint and still CLICKABLE, eating the tap that
  // turns the page. Every other face cleans what it is given for the same
  // reason (`ui/lvgl/provision_face.cpp:73` — "  lv_obj_clean(screen);").
  lv_obj_clean(screen_);

  // The screen object outlives every face and carries the last one's styles
  // into the next -- NavFace leaves it a flex column, and a flex parent ignores
  // every absolute alignment below it.
  lv_obj_remove_style_all(screen_);
  lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(
      screen_, resolved(ColorRole::BackgroundPrimary, config.theme, config.pixel_cost),
      LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, LV_PART_MAIN);

  const bool big = large();
  const Metrics &m = config.metrics;
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config.theme, config.pixel_cost);
  const lv_color_t primary =
      resolved(ColorRole::TextPrimary, config.theme, config.pixel_cost);
  const lv_color_t accent =
      resolved(ColorRole::AccentPrimary, config.theme, config.pixel_cost);

  const lv_font_t *state_font =
      big ? &attadipa_nunito_sans_28 : &attadipa_nunito_sans_20;
  const lv_font_t *body_font =
      big ? &attadipa_nunito_sans_20 : &attadipa_nunito_sans_16;
  const lv_font_t *small_font =
      big ? &attadipa_nunito_sans_16 : &attadipa_nunito_sans_14;
  const lv_font_t *tiny_font = &attadipa_nunito_sans_14;

  // The screen's own text font, not decoration: `lv_obj_remove_style_all()`
  // above took the inherited one with it, and what is left is LVGL's built-in
  // default, which has no Cyrillic in it at all. Every other face sets this for
  // the same reason -- `ui/lvgl/nav_face.cpp:113` —
  // "  lv_obj_set_style_text_font(screen, body_font, LV_PART_MAIN);".
  lv_obj_set_style_text_font(screen_, body_font, LV_PART_MAIN);

  title_ = label(screen_, tiny_font, muted);
  lv_obj_set_style_text_letter_space(title_, tracking_wide(m), LV_PART_MAIN);

  watch_ = lv_obj_create(screen_);
  bare(watch_);
  lv_obj_set_style_bg_opa(watch_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_color(watch_, primary, LV_PART_MAIN);
  lv_obj_set_style_border_width(watch_, stroke(m), LV_PART_MAIN);
  lv_obj_set_style_border_opa(watch_, LV_OPA_COVER, LV_PART_MAIN);

  watch_dot_ = lv_obj_create(screen_);
  bare(watch_dot_);
  lv_obj_set_style_radius(watch_dot_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(watch_dot_, primary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(watch_dot_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(watch_dot_, 0, LV_PART_MAIN);

  for (lv_obj_t *&rung : rung_) {
    rung = lv_obj_create(screen_);
    bare(rung);
    lv_obj_set_style_border_width(rung, 0, LV_PART_MAIN);
  }

  halo_ = lv_obj_create(screen_);
  bare(halo_);
  lv_obj_set_style_radius(halo_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(halo_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(halo_, stroke(m), LV_PART_MAIN);

  socket_ = lv_obj_create(screen_);
  bare(socket_);
  lv_obj_set_style_radius(socket_, LV_RADIUS_CIRCLE, LV_PART_MAIN);

  intruder_ = lv_obj_create(screen_);
  bare(intruder_);
  lv_obj_set_style_radius(intruder_, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(intruder_, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(intruder_, LV_OPA_COVER, LV_PART_MAIN);

  state_ = label(screen_, state_font, muted);
  lv_obj_set_style_text_letter_space(state_, tracking_tight(m), LV_PART_MAIN);
  note_ = label(screen_, small_font, muted);
  way_out_ = label(screen_, small_font, accent);
  node_key_ = label(screen_, body_font, muted);
  node_name_ = label(screen_, small_font, primary);
  pinned_ = label(screen_, small_font, muted);
  answered_ = label(screen_, small_font,
                    resolved(ColorRole::Warning, config.theme, config.pixel_cost));

  rule_ = lv_obj_create(screen_);
  bare(rule_);
  lv_obj_set_style_border_width(rule_, 0, LV_PART_MAIN);
  lv_obj_set_style_bg_color(rule_, muted, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(rule_, LV_OPA_20, LV_PART_MAIN);

  msg_heading_ = label(screen_, tiny_font, muted);
  lv_obj_set_style_text_letter_space(msg_heading_, tracking_tight(m), LV_PART_MAIN);
  msg_ = label(screen_, body_font, primary);
  msg_meta_ = label(screen_, tiny_font, muted);
  for (int i = 0; i < 3; ++i) {
    value_[i] = label(screen_, body_font, primary);
    label_[i] = label(screen_, tiny_font, muted);
    lv_obj_set_style_text_letter_space(label_[i], tracking_tight(m), LV_PART_MAIN);
  }

  built_ = true;
  update(text);
}

void MeshFace::update(const apps::MeshText &text) {
  if (!built_) {
    return;
  }
  if (shown_valid_ && std::memcmp(&shown_, &text, sizeof(text)) == 0) {
    return;
  }
  shown_ = text;
  shown_valid_ = true;
  lay_out(text);
  paint_channel(text);
}

// Where everything goes, and whether it goes anywhere at all.
//
// Two compositions, not one scaled: with a link the glyph shares the panel with
// what came over it, and without one the glyph moves down into the space the
// message block would have used. That space is deliberately left empty --
// filling it with an empty heading and a row of zeroes is what the screen used
// to do, and it is what made a watch with no radio look like a watch with a bad
// signal.
void MeshFace::lay_out(const apps::MeshText &text) {
  const bool big = large();
  const bool linked = text.has_message;
  const int inset = std::max(0, (big ? 502 : 240) -
                                  static_cast<int>(config_.height_px));
  const auto w = static_cast<std::int32_t>(config_.width_px);

  show(title_, text.title);
  lv_obj_align(title_, LV_ALIGN_TOP_LEFT, big ? 32 : 20, big ? 24 : 12);

  show(state_, text.state);
  lv_obj_set_style_text_color(
      state_, resolved(role_for(text.link), config_.theme, config_.pixel_cost),
      LV_PART_MAIN);
  lv_obj_set_width(state_, w);
  lv_obj_set_style_text_align(state_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  const std::int32_t margin = big ? 32 : 20;
  lv_obj_set_width(note_, w);
  // Content height here and bounded only where something is positioned under
  // it. Both are set on every layout because the same label serves screens
  // that answer this differently, and a height left over from the previous
  // `text` would clip prose that has room.
  lv_obj_set_height(note_, LV_SIZE_CONTENT);
  lv_label_set_long_mode(note_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(way_out_, w);
  lv_obj_set_style_text_align(way_out_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

  if (linked) {
    lv_obj_align(state_, LV_ALIGN_TOP_LEFT, 0, (big ? 196 : 84) - inset);
    // 240 px spends the row under the state word on the node key, which is the
    // identity. Drawing the note there too put one on top of the other.
    if (big) {
      show(note_, text.note);
      // Under the state word, by the width of the gap rather than by a row
      // number: `state_` is the largest type on the screen and the only thing
      // above this.
      lv_obj_align_to(note_, state_, LV_ALIGN_OUT_BOTTOM_MID, 0,
                      config_.metrics.px(dp_of(Space::Xs)));
    } else {
      hide(note_);
    }

    show(node_key_, text.node_key);
    show(node_name_, text.node_name);
    // ONE LINE HERE TOO, AND THIS ROW'S TEXT IS A PEER'S OWN CHOICE.
    //
    // `node_name` arrives in `RESP_CODE_SELF_INFO`, so its length and its bytes
    // are decided off the link rather than here. Created bare, the label has
    // content height and `LV_LABEL_LONG_WRAP`, so a name carrying a line break
    // draws a second row -- down over the rule at y=352 and the message heading
    // under it. Every name this file renders in a fixture is one short word,
    // which is exactly why the two rows below caught this and this one did not.
    lv_obj_set_width(node_name_, w - margin * 2);
    lv_obj_set_height(node_name_,
                      lv_font_get_line_height(
                          lv_obj_get_style_text_font(node_name_, LV_PART_MAIN)));
    lv_label_set_long_mode(node_name_, LV_LABEL_LONG_DOT);
    lv_obj_align(node_name_, LV_ALIGN_TOP_LEFT, margin, (big ? 312 : 132) - inset);
    if (!big) {
      // 240 px has room for the key or the name, not both, and the key is the
      // identity -- the two bench nodes had interchangeable names.
      hide(node_name_);
      lv_obj_set_width(node_key_, w);
      lv_obj_set_style_text_align(node_key_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    } else {
      lv_obj_set_width(node_key_, LV_SIZE_CONTENT);
      lv_obj_set_style_text_align(node_key_, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    }
    lv_obj_align(node_key_, LV_ALIGN_TOP_LEFT, big ? margin : 0,
                 (big ? 284 : 112) - inset);
    hide(pinned_);
    hide(answered_);
    hide(way_out_);

    lv_obj_remove_flag(rule_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(rule_, w - margin * 2, config_.metrics.px(Dp{1}));
    lv_obj_align(rule_, LV_ALIGN_TOP_LEFT, margin, (big ? 352 : 136) - inset);

    show(msg_heading_, text.message_heading);
    // The heading says three things and this is the third: whether the block
    // is live, and whether what it holds is all of what arrived. `Warning` is
    // the strongest colour either palette has, and it outranks the live/last
    // distinction here rather than replacing it -- the heading's own words
    // still carry that, `LAST KNOWN · CUT` against `MESSAGE · CUT`.
    //
    // THE EMPHASIS IS WEIGHT, NOT HUE, BECAUSE THE DAY PALETTE HAS NO HUE TO
    // SPEND HERE. `color.warning` measures 1.93:1 on a day surface
    // (`docs/ui/DESIGN_SYSTEM.md:139` — "| `color.warning` | **2.19** | **1.93** | **1.73** |"),
    // under the 4.5:1 a word needs, and `CUT` is a word that must be read. An
    // earlier version of this line tinted it anyway and defended that as a
    // price the row already paid. It is not: that parity holds against
    // `AccentPrimary` for `Resting` only, and for `Linked` the colour being
    // replaced is `TextMuted` at 4.95:1, which passes -- so tinting moved the
    // one element carrying the cue from passing to failing.
    // `docs/ui/DESIGN_SYSTEM.md:166` — "word — it is drawn on a dark chip rather than tinted, or it is drawn in"
    // names the two treatments for an accent that must be read; neither is
    // free here, and neither is needed. `TextPrimary` is 9.78:1 on a surface
    // and 9.93:1 at night, it is stronger than both colours it replaces, and
    // it says "read this" without asking the palette for a legibility it does
    // not have on the day theme.
    lv_obj_set_style_text_color(
        msg_heading_,
        resolved(text.message_partial
                     ? ColorRole::TextPrimary
                     : (text.live ? ColorRole::TextMuted
                                  : ColorRole::AccentPrimary),
                 config_.theme, config_.pixel_cost),
        LV_PART_MAIN);
    show(msg_, text.message);
    lv_obj_set_style_text_opa(msg_, text.live ? LV_OPA_COVER : LV_OPA_60,
                              LV_PART_MAIN);
    lv_obj_set_width(msg_, w - margin * 2);
    // ONE LINE, AND AN ELLIPSIS FOR THE REST.
    //
    // `LV_LABEL_LONG_DOT` puts the dots in only where the height is fixed;
    // with the height left at content the label grew downwards instead, and a
    // message longer than the fixture's ran straight through the row beneath
    // it. The composition reserves one line for the message on both panels, so
    // one line is what it is given -- and that is what makes it safe for
    // `apps::MeshText::message` to carry the whole of what arrived rather than
    // as much of it as a buffer had room for.
    lv_obj_set_height(msg_, lv_font_get_line_height(
                                lv_obj_get_style_text_font(msg_, LV_PART_MAIN)));
    lv_label_set_long_mode(msg_, LV_LABEL_LONG_DOT);

    // The heading gets the same treatment, and now that it has to.
    //
    // It is a bare label -- content height, `LV_LABEL_LONG_WRAP` -- and until
    // the cut wording arrived the string on it was nine glyphs, so the trap
    // had nothing to spring on. `СООБЩЕНИЕ · ОБРЕЗАНО` is twenty, 181 px of a
    // 240 px panel by the font's own advance widths, and the next translation
    // or the next word after `CUT` is what a rendered test should not have to
    // be re-run to survive. One line and an ellipsis leaves the message row
    // below it out of reach of anything a catalogue can say.
    lv_obj_set_width(msg_heading_, w - margin * 2);
    lv_obj_set_height(
        msg_heading_,
        lv_font_get_line_height(
            lv_obj_get_style_text_font(msg_heading_, LV_PART_MAIN)));
    lv_label_set_long_mode(msg_heading_, LV_LABEL_LONG_DOT);

    if (!big) {
      lv_obj_set_style_text_align(msg_heading_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
      lv_obj_set_width(msg_heading_, w);
      lv_obj_set_style_text_align(msg_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
    lv_obj_align(msg_heading_, LV_ALIGN_TOP_LEFT, big ? margin : 0,
                 (big ? 368 : 148) - inset);
    lv_obj_align(msg_, LV_ALIGN_TOP_LEFT, margin, (big ? 394 : 168) - inset);

    // Sender and delivery share a line: on their own neither is evidence of
    // anything, and together they are the whole story of one message.
    if (text.sender[0] != '\0') {
      lv_label_set_text_fmt(msg_meta_, "%s  ·  %s", text.sender, text.delivery);
      lv_obj_remove_flag(msg_meta_, LV_OBJ_FLAG_HIDDEN);
    } else {
      show(msg_meta_, text.delivery);
    }
    lv_obj_set_width(msg_meta_, w - margin * 2);
    // ONE LINE HERE TOO, AND FOR A SHARPER REASON THAN THE MESSAGE HAD.
    //
    // `LV_LABEL_LONG_DOT` needs a fixed height or it wraps and grows downward
    // (the message row two lines up carries the long version of this), and what
    // this row renders is not a fixture: `sender` is a peer's advertised name
    // off the link, up to `kMeshPeerNameBytes`, joined here with the delivery
    // word. A 32-character name plus "не доставлено" does not fit 346 px at
    // `nunito_sans_14`, so the second line landed on the measurements at y=452
    // -- six rows of collision chosen by whoever named the node, and invisible
    // to the simulator, whose fixture name is fifteen characters.
    lv_obj_set_height(msg_meta_,
                      lv_font_get_line_height(
                          lv_obj_get_style_text_font(msg_meta_, LV_PART_MAIN)));
    lv_label_set_long_mode(msg_meta_, LV_LABEL_LONG_DOT);
    if (big) {
      // Under the message, because the message is the one row on this screen
      // whose height is not the layout's to choose.
      lv_obj_align_to(msg_meta_, msg_, LV_ALIGN_OUT_BOTTOM_LEFT, 0,
                      config_.metrics.px(dp_of(Space::Xs)));
    } else {
      hide(msg_meta_);  // 240 px spends its last rows on the measurements
    }

    const std::int32_t column[3] = {big ? 32 : 22, big ? 155 : 98,
                                    big ? 268 : 174};
    const char *values[3] = {text.snr, text.peers, text.mtu};
    const char *labels[3] = {text.snr_label, text.peers_label, text.mtu_label};
    // EACH COLUMN ENDS WHERE THE NEXT ONE STARTS, AND SAYS SO.
    //
    // All three of these are wire-derived and none of them is a fixture: SNR
    // is a signed quarter-dB with two decimals, MTU is negotiated, and the
    // peer count is now two numbers wherever the retained set is capped. Left
    // at content width they grow rightwards into the neighbour -- `16/65535`
    // beside a three-digit MTU rendered as `16/65535244`, one number as far as
    // anybody reading it is concerned. Bounded and ellipsised is the treatment
    // the message and the sender rows above already get, and for the same
    // reason: what decides the width of these is off the link, not here.
    // The right edge is the same margin the rule and the message already use,
    // not a hand-picked inset: 12 on 240 px put this row's box 8 px right of
    // every rule above it. The gutter is a physical length too -- 4 dp is 8 px
    // at 314 dpi and 6 px at 220, which `big ? 8 : 4` got wrong on the smaller
    // panel in the direction that matters, too tight.
    const std::int32_t last_edge = w - margin;
    const std::int32_t gutter = config_.metrics.px(dp_of(Space::Xs));
    for (int i = 0; i < 3; ++i) {
      const std::int32_t gap =
          (i < 2 ? column[i + 1] : last_edge) - column[i] - gutter;
      show(value_[i], values[i]);
      show(label_[i], labels[i]);
      lv_obj_set_style_text_opa(value_[i], text.live ? LV_OPA_COVER : LV_OPA_50,
                                LV_PART_MAIN);
      for (lv_obj_t *cell : {value_[i], label_[i]}) {
        lv_obj_set_width(cell, gap);
        lv_obj_set_height(cell,
                          lv_font_get_line_height(
                              lv_obj_get_style_text_font(cell, LV_PART_MAIN)));
        lv_label_set_long_mode(cell, LV_LABEL_LONG_DOT);
      }
      lv_obj_align(value_[i], LV_ALIGN_TOP_LEFT, column[i], (big ? 452 : 192) - inset);
      lv_obj_align(label_[i], LV_ALIGN_TOP_LEFT, column[i], (big ? 478 : 214) - inset);
    }
  } else {
    lv_obj_align(state_, LV_ALIGN_TOP_LEFT, 0, (big ? 262 : 132) - inset);
    show(note_, text.note);
    const std::int32_t note_y = (big ? 304 : 162) - inset;
    lv_obj_align(note_, LV_ALIGN_TOP_LEFT, 0, note_y);

    hide(node_key_);
    hide(node_name_);
    hide(rule_);
    hide(msg_heading_);
    hide(msg_);
    hide(msg_meta_);
    for (int i = 0; i < 3; ++i) {
      hide(value_[i]);
      hide(label_[i]);
    }

    show(pinned_, text.pinned);
    show(answered_, text.answered);
    lv_obj_set_width(pinned_, w);
    lv_obj_set_width(answered_, w);
    lv_obj_set_style_text_align(pinned_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(answered_, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // What is actually drawn, not which screen this is. `show()` has just
    // hidden both key labels wherever the app filled neither, and a layout
    // that reserved their rows anyway clipped a note against nothing.
    const bool keys = text.pinned[0] != '\0';

    // On 240 px the note, the first key row and the way out cannot all be
    // drawn: 162 to the panel edge is four rows of prose and the four of them
    // want five. Which one gives is not a layout question.
    //
    // Where the screen names a way out, the keys take the note's row and the
    // note goes: `TurnedAway` is the case that made the rule -- "that node
    // turned you away" says nothing the pinned and answered keys do not -- and
    // an `Unprovisioned` transport that has faulted with a refusal latched is
    // the same shape, `NoNode` with both keys and "hold the clock to change
    // it". Stacking the keys under the note there put `answered_` at 208 and
    // the way out at 210, one on top of the other.
    //
    // Where there is no way out -- a terminal fault -- the note is the only
    // place the reset is named, and the keys are the evidence that the reset
    // will not clear everything (`reset_session()` keeps the pin and the
    // refusal on purpose), so there the keys move under the note instead.
    //
    // On 410 px every row has its own and neither question arises.
    const bool keys_replace_note = !big && keys && text.way_out[0] != '\0';
    if (keys_replace_note) {
      hide(note_);
    }
    // Where they go is fixed rows in every case, and where the note is kept it
    // is the note that gives. Hanging the keys off `note_` with `align_to` made
    // the bottom of the screen a property of a translated string: `note_` has
    // content height, so a two-line note pushed `answered_` off a 240 px panel
    // -- and on 410 px, where the keys are further down, a two-line note ran
    // into `pinned_` instead. The keys are single-line identities and cannot
    // ellipsise usefully; the prose can, so it is bounded to the whole lines
    // that fit above the first key row and gets `LV_LABEL_LONG_DOT`. The pixels
    // are the ones this screen already had: 162 + one line + `Xs` is 185.
    const std::int32_t gap = config_.metrics.px(dp_of(Space::Xs));
    const std::int32_t line = lv_font_get_line_height(
        lv_obj_get_style_text_font(note_, LV_PART_MAIN));
    std::int32_t pinned_y = 0;
    std::int32_t answered_y = 0;
    if (big || keys_replace_note) {
      pinned_y = (big ? 350 : 162) - inset;
      answered_y = (big ? 376 : 184) - inset;
    } else {
      pinned_y = note_y + line + gap;
      answered_y = pinned_y + line + gap;
    }
    if (keys && !keys_replace_note) {
      const std::int32_t rows = (pinned_y - gap - note_y) / line;
      lv_obj_set_height(note_, (rows > 1 ? rows : 1) * line);
      lv_label_set_long_mode(note_, LV_LABEL_LONG_DOT);
    }
    lv_obj_align(pinned_, LV_ALIGN_TOP_LEFT, 0, pinned_y);
    lv_obj_align(answered_, LV_ALIGN_TOP_LEFT, 0, answered_y);

    show(way_out_, text.way_out);
    lv_obj_align(way_out_, LV_ALIGN_TOP_LEFT, 0, (big ? 444 : 210) - inset);
  }
}

// The glyph. Everything here is derived from the panel and from `text.link`;
// nothing is derived from a string, so a translation cannot move a pixel.
void MeshFace::paint_channel(const apps::MeshText &text) {
  const bool big = large();
  const bool linked = text.has_message;
  const int inset = std::max(0, (big ? 502 : 240) -
                                  static_cast<int>(config_.height_px));
  const auto cx = static_cast<std::int32_t>(config_.width_px) / 2;
  const std::int32_t cy = (linked ? (big ? 128 : 58) : (big ? 176 : 78)) - inset / 2;
  const std::int32_t r = (linked ? (big ? 26 : 15) : (big ? 30 : 18)) -
      (!big && linked ? inset / 5 : 0);
  const std::int32_t span = big ? 250 : 150;
  const lv_color_t colour =
      resolved(role_for(text.link), config_.theme, config_.pixel_cost);
  const lv_color_t muted =
      resolved(ColorRole::TextMuted, config_.theme, config_.pixel_cost);

  // A watch with nothing to reach is not sitting at the left end of a link that
  // is not drawn -- it is on its own, in the middle.
  const bool alone = text.link == apps::MeshLink::NoNode;
  const std::int32_t x0 = alone ? cx : cx - span / 2;
  const std::int32_t x1 = cx + span / 2;

  const std::int32_t side = r * 18 / 10;
  lv_obj_set_size(watch_, side, side);
  lv_obj_set_style_radius(watch_, r * 55 / 100, LV_PART_MAIN);
  lv_obj_align(watch_, LV_ALIGN_TOP_LEFT, x0 - side / 2, cy - side / 2);
  const std::int32_t dot = r * 44 / 100;
  lv_obj_set_size(watch_dot_, dot, dot);
  lv_obj_align(watch_dot_, LV_ALIGN_TOP_LEFT, x0 - dot / 2, cy - dot / 2);

  if (alone) {
    for (lv_obj_t *rung : rung_) {
      hide(rung);
    }
    hide(socket_);
    hide(halo_);
    hide(intruder_);
    return;
  }

  const std::int32_t thickness = big ? 5 : 4;
  const std::int32_t gap = span - r * 26 / 10;
  const std::int32_t step = gap / apps::kChannelRungs;
  for (unsigned i = 0; i < apps::kChannelRungs; ++i) {
    lv_obj_t *rung = rung_[i];
    // THE GAP IS THE MESSAGE. A severed link is not a dim link: the middle
    // segment is not drawn at all, so "broken" reads at arm's length and
    // without colour, which is the one state a wearer has to act on.
    if (text.link == apps::MeshLink::Broken && i == apps::kChannelRungs / 2) {
      hide(rung);
      continue;
    }
    const bool lit = i < text.rungs_lit;
    lv_obj_remove_flag(rung, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(rung, step * 64 / 100, thickness);
    lv_obj_set_style_radius(rung, thickness / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rung, lit ? colour : muted, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rung,
                            text.link == apps::MeshLink::Resting ? LV_OPA_30
                            : lit                                ? LV_OPA_COVER
                                                                 : LV_OPA_20,
                            LV_PART_MAIN);
    lv_obj_align(rung, LV_ALIGN_TOP_LEFT,
                 x0 + r * 13 / 10 +
                     static_cast<std::int32_t>(i) * step + step * 18 / 100,
                 cy - thickness / 2);
  }

  lv_obj_remove_flag(socket_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(socket_, r * 2, r * 2);
  lv_obj_align(socket_, LV_ALIGN_TOP_LEFT, x1 - r, cy - r);
  const bool full = text.link == apps::MeshLink::Linked;
  lv_obj_set_style_bg_color(socket_, colour, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(socket_, full ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(socket_, full ? 0 : stroke(config_.metrics),
                                LV_PART_MAIN);
  lv_obj_set_style_border_color(socket_, colour, LV_PART_MAIN);
  // An empty socket that is *waiting* and one that is *not coming* are drawn at
  // different strengths: the wearer of a watch that is reaching should be able
  // to tell it apart from one that has nothing to reach for.
  lv_obj_set_style_border_opa(
      socket_,
      (text.link == apps::MeshLink::NoRadio ||
       text.link == apps::MeshLink::TurnedAway ||
       text.link == apps::MeshLink::Resting)
          ? LV_OPA_40
          : LV_OPA_COVER,
      LV_PART_MAIN);

  if (full) {
    lv_obj_remove_flag(halo_, LV_OBJ_FLAG_HIDDEN);
    const std::int32_t halo = r * 31 / 10;
    lv_obj_set_size(halo_, halo, halo);
    lv_obj_set_style_border_color(halo_, colour, LV_PART_MAIN);
    lv_obj_set_style_border_opa(halo_, LV_OPA_30, LV_PART_MAIN);
    lv_obj_align(halo_, LV_ALIGN_TOP_LEFT, x1 - halo / 2, cy - halo / 2);
  } else {
    hide(halo_);
  }

  if (text.link == apps::MeshLink::TurnedAway) {
    lv_obj_remove_flag(intruder_, LV_OBJ_FLAG_HIDDEN);
    const std::int32_t size = r * 116 / 100;
    lv_obj_set_size(intruder_, size, size);
    lv_obj_set_style_bg_color(intruder_, colour, LV_PART_MAIN);
    lv_obj_align(intruder_, LV_ALIGN_TOP_LEFT, x1 - size / 2, cy + r * 205 / 100 - size / 2);
  } else {
    hide(intruder_);
  }
}

void MeshFace::clear() {
  if (screen_ != nullptr) {
    lv_obj_clean(screen_);
  }
  screen_ = nullptr;
  title_ = nullptr;
  watch_ = nullptr;
  watch_dot_ = nullptr;
  for (lv_obj_t *&rung : rung_) {
    rung = nullptr;
  }
  socket_ = nullptr;
  halo_ = nullptr;
  intruder_ = nullptr;
  state_ = nullptr;
  note_ = nullptr;
  way_out_ = nullptr;
  node_key_ = nullptr;
  node_name_ = nullptr;
  pinned_ = nullptr;
  answered_ = nullptr;
  rule_ = nullptr;
  msg_heading_ = nullptr;
  msg_ = nullptr;
  msg_meta_ = nullptr;
  for (int i = 0; i < 3; ++i) {
    value_[i] = nullptr;
    label_[i] = nullptr;
  }
  built_ = false;
  shown_valid_ = false;
}

} // namespace attadipa::ui
