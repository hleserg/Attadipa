#include <cstdio>
#include <string>
#include <vector>

#include "attadipa/ui/color.h"
#include "attadipa/ui/tokens.h"

// Host tests for the design tokens (T-009).
//
// Nothing here has been shown on a panel, and that is the point of most of it:
// the values are proposed, and these tests pin the *properties* that must hold
// whatever the values become. Two of them found something.

using namespace attadipa::ui;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

// The two panels, by density rather than by name — which is the whole point of
// the ui library not linking platform. These numbers are what
// DisplayInfo::dpi() computes from the profiles: 240x240 over a working 1.3"
// (the conservative reading of a CONFLICTING diagonal, HARDWARE_MATRIX) and
// 410x502 over 2.06".
constexpr std::uint16_t kTWatchDpi    = 261;
constexpr std::uint16_t kWaveshareDpi = 315;

const ColorRole kAllRoles[] = {
    ColorRole::BackgroundPrimary, ColorRole::BackgroundSurface, ColorRole::BackgroundRaised,
    ColorRole::TextPrimary,       ColorRole::TextMuted,         ColorRole::AccentPrimary,
    ColorRole::AccentGlow,        ColorRole::Success,           ColorRole::Warning,
    ColorRole::Danger,            ColorRole::Navigation,        ColorRole::BorderSubtle,
};

const Space       kAllSpace[]  = {Space::Xs, Space::Sm, Space::Md,
                                  Space::Lg, Space::Xl, Space::Xxl};
const Radius      kAllRadius[] = {Radius::Sm, Radius::Md, Radius::Lg, Radius::Pill};
const IconSize    kAllIcon[]   = {IconSize::Sm, IconSize::Md, IconSize::Lg, IconSize::Xl};
const ImageSize   kAllImage[]  = {ImageSize::Inline, ImageSize::Spot, ImageSize::Hero,
                                  ImageSize::HeroLarge};
const TouchTarget kAllTouch[]  = {TouchTarget::Adult, TouchTarget::ChildMode};

// ---------------------------------------------------------------------------

// The property the tokens exist for, and the one a pixel count cannot express.
//
// The same token must come out the same *physical* size on two panels whose
// densities differ by 21 %. Asserting the pixel counts would prove nothing —
// they differ on purpose — so this asserts the millimetres.
void test_one_token_is_one_physical_size_on_both_panels()
{
    const Metrics watch = Metrics::for_dpi(kTWatchDpi);
    const Metrics wave  = Metrics::for_dpi(kWaveshareDpi);

    for (Space s : kAllSpace) {
        const std::int32_t a = watch.micrometres(dp_of(s));
        const std::int32_t b = wave.micrometres(dp_of(s));
        const std::int32_t difference = a > b ? a - b : b - a;

        // 60 um is a quarter of a pixel at either density: the residue of
        // rounding a Dp to a whole pixel, not a discrepancy in the token.
        if (difference > 60) {
            std::fprintf(stderr, "FAIL: %s is %d um on one panel and %d um on the other\n",
                         name_of(s), a, b);
            ++failures;
        }
    }

    // And the pixel counts really do differ, or the test above is passing
    // because nothing is being scaled at all.
    CHECK(watch.px(dp_of(Space::Sm)) != wave.px(dp_of(Space::Sm)));
    CHECK(watch.px(dp_of(Space::Sm)) == 13);
    CHECK(wave.px(dp_of(Space::Sm)) == 16);
}

// A gap that rounds away is not a small gap. It is two elements touching, on
// one panel only, discovered by whoever owns that panel.
void test_no_spacing_collapses_to_nothing()
{
    const std::uint16_t densities[] = {80, 120, kReferenceDpi, kTWatchDpi, kWaveshareDpi, 640};
    for (std::uint16_t dpi : densities) {
        const Metrics m = Metrics::for_dpi(dpi);
        for (Space s : kAllSpace) {
            if (m.px(dp_of(s)) < 1) {
                std::fprintf(stderr, "FAIL: %s collapses to 0 px at %u dpi\n", name_of(s), dpi);
                ++failures;
            }
        }
        CHECK(m.px(Dp{0}) == 0);   // and zero still means zero
    }

    // The tokens themselves are safe at any plausible density — the smallest is
    // 4 dp, which survives rounding down to 20 dpi. The value that is not safe
    // is the one UI code writes directly and constantly: a 1 dp hairline, a
    // divider, a focus ring. Below 80 dpi that rounds to nothing, so the
    // guarantee is stated about the function rather than about the two panels
    // we happen to have, and checked where it actually bites.
    for (std::uint16_t dpi = 1; dpi <= 400; ++dpi) {
        const Metrics m = Metrics::for_dpi(dpi);
        if (m.px(Dp{1}) < 1) {
            std::fprintf(stderr, "FAIL: a 1 dp hairline vanishes at %u dpi\n", dpi);
            ++failures;
        }
        // And it stays a length in the other direction: a negative offset must
        // not round to zero either, or a nudge becomes no nudge.
        if (m.px(Dp{-1}) > -1) {
            std::fprintf(stderr, "FAIL: a -1 dp offset vanishes at %u dpi\n", dpi);
            ++failures;
        }
    }
}

// Touch targets are the case where getting this wrong is not cosmetic.
void test_a_touch_target_is_physical_and_clears_the_minimum()
{
    const Metrics panels[] = {Metrics::for_dpi(kTWatchDpi), Metrics::for_dpi(kWaveshareDpi)};
    for (const Metrics& m : panels) {
        for (TouchTarget t : kAllTouch) {
            // 44 dp is ~7 mm, which is the figure the guidance these tokens
            // borrow their reference density from actually intends.
            const std::int32_t um = m.micrometres(dp_of(t));
            if (um < 6900) {
                std::fprintf(stderr, "FAIL: %s is only %d um at %u dpi\n", name_of(t), um,
                             m.dpi());
                ++failures;
            }
        }
        // Child Mode is larger, and by more than rounding.
        CHECK(m.px(dp_of(TouchTarget::ChildMode)) > m.px(dp_of(TouchTarget::Adult)) + 4);
    }

    // Written in raw pixels instead, 44 would be 4.3 mm on the Waveshare — well
    // under the minimum, on one board, from the same source line. That is the
    // bug the Dp type exists to make unwritable.
    const Metrics wave = Metrics::for_dpi(kWaveshareDpi);
    CHECK(44 * 25400 / static_cast<std::int32_t>(wave.dpi()) < 6900);
}

// `Pill` is a rule, not a length. Resolving it as one gives 1630 px at 261 dpi,
// which is larger than either panel.
void test_pill_is_a_rule_and_not_a_length()
{
    CHECK(is_pill(Radius::Pill));
    CHECK(!is_pill(Radius::Md));
    CHECK(dp_of(Radius::Pill) == Dp{0});

    const Metrics m = Metrics::for_dpi(kTWatchDpi);
    CHECK(radius_px(Radius::Pill, m, 48) == 24);       // half the shorter side
    CHECK(radius_px(Radius::Md, m, 48) == m.px(Dp{12}));
    CHECK(radius_px(Radius::Md, m, 48) != 24);         // and not the pill answer
}

// Durations are not lengths either: a denser panel does not change how long a
// transition takes.
void test_motion_is_not_scaled_by_density()
{
    CHECK(milliseconds_of(Motion::Instant) == 0);
    CHECK(milliseconds_of(Motion::Fast) < milliseconds_of(Motion::Base));
    CHECK(milliseconds_of(Motion::Base) < milliseconds_of(Motion::Slow));
}

// ---------------------------------------------------------------------------

// `color.danger` has no value and must keep not having one. There is no red in
// either owner palette, and a red invented here would be a visual-identity
// decision made by a compiler.
void test_danger_is_unknown_in_both_themes()
{
    CHECK(!color(ColorRole::Danger, Theme::Day).has_value());
    CHECK(!color(ColorRole::Danger, Theme::Night).has_value());
    CHECK(!is_defined_for(ColorRole::Danger, Theme::Day));
    CHECK(contrast_against_page_centi(ColorRole::Danger, Theme::Day) == 0);

    // And it is the *only* role with no day value. A second one appearing means
    // somebody deleted a colour rather than that the palette grew a hole.
    int undefined_in_day = 0;
    for (ColorRole role : kAllRoles) {
        if (!color(role, Theme::Day).has_value()) {
            ++undefined_in_day;
        }
    }
    CHECK(undefined_in_day == 1);
}

// Night is not inverted day (final §47), so a day background has no defensible
// night value — and falling through would put a Soft Clay card on a dark page.
void test_a_background_never_falls_through_to_day()
{
    for (ColorRole role : kAllRoles) {
        if (kind_of(role) != ColorKind::Background) {
            continue;
        }
        if (!is_defined_for(role, Theme::Night)) {
            if (color(role, Theme::Night).has_value()) {
                std::fprintf(stderr, "FAIL: %s inherited a day background at night\n",
                             name_of(role));
                ++failures;
            }
        }
    }

    // Which is currently exactly one role, and DESIGN_SYSTEM's night table
    // genuinely does not define it.
    CHECK(!color(ColorRole::BackgroundRaised, Theme::Night).has_value());
    CHECK(color(ColorRole::BackgroundPrimary, Theme::Night).has_value());

    // The failure this rule prevents, made explicit: day text on the night page
    // would be Ink Olive on Ink Olive.
    CHECK(*color(ColorRole::TextPrimary, Theme::Day) ==
          *color(ColorRole::BackgroundPrimary, Theme::Night));
    CHECK(*color(ColorRole::TextPrimary, Theme::Night) !=
          *color(ColorRole::BackgroundPrimary, Theme::Night));
}

// The fall-through for foregrounds is only defensible while it stays legible,
// and legible is computable.
void test_every_night_foreground_survives_the_night_page()
{
    for (ColorRole role : kAllRoles) {
        if (kind_of(role) != ColorKind::Foreground) {
            continue;
        }
        if (!color(role, Theme::Night).has_value()) {
            continue;   // Danger, and it is UNKNOWN on purpose
        }
        if (!legible_as_graphic(role, Theme::Night)) {
            std::fprintf(stderr, "FAIL: %s is %u:100 on the night page\n", name_of(role),
                         contrast_against_page_centi(role, Theme::Night));
            ++failures;
        }
    }

    // The tightest of them, pinned so that a palette change that erodes it is
    // visible as a diff rather than as a screen nobody can read at 3 a.m.
    CHECK(contrast_against_page_centi(ColorRole::Success, Theme::Night) >= 350);
}

// The finding.
//
// The night palette is healthy and the DAY palette is not: on Warm Ivory,
// Attadipa Orange is 2.19:1 and Glow Amber is 1.44:1 — both far below the 3:1
// that a glyph or a one-pixel outline needs, let alone the 4.5:1 for a word.
//
// These are the owner's colours, taken from final §42, and this file does not
// get to change them. What it can do is refuse to let anybody discover it on a
// panel. The numbers are pinned as facts so that a future palette edit shows up
// here first, and the consequence is stated once: on the day theme the accents
// are decoration, and meaning travels by icon and word.
//
// This is also, independently, why DESIGN_SYSTEM §3.1 forbids signalling state
// by colour alone. That rule was written for colour-blindness; it turns out to
// be load-bearing for everyone.
void test_the_day_accents_cannot_carry_meaning_alone()
{
    CHECK(!legible_as_graphic(ColorRole::AccentPrimary, Theme::Day));
    CHECK(!legible_as_graphic(ColorRole::AccentGlow, Theme::Day));
    CHECK(!legible_as_graphic(ColorRole::Success, Theme::Day));
    CHECK(!legible_as_graphic(ColorRole::Navigation, Theme::Day));
    CHECK(!legible_as_graphic(ColorRole::BorderSubtle, Theme::Day));

    // Two do carry it, and they are the two the text roles are for.
    CHECK(legible_as_body_text(ColorRole::TextPrimary, Theme::Day));
    CHECK(legible_as_body_text(ColorRole::TextMuted, Theme::Day));

    // The measured values, to a hundredth, so the finding cannot rot into an
    // approximate memory of a finding.
    CHECK(contrast_against_page_centi(ColorRole::AccentPrimary, Theme::Day) == 219);
    CHECK(contrast_against_page_centi(ColorRole::AccentGlow, Theme::Day) == 144);
    CHECK(contrast_against_page_centi(ColorRole::TextPrimary, Theme::Day) == 1110);
}

// The second finding, and the one that would never have been seen by looking.
//
// `color.text.muted` is comfortable on the day page (5.62:1) and still passes on
// a card (4.95:1) — and then fails on a *raised* card at **4.44:1**, six
// hundredths under the 4.5:1 that body text needs. A timestamp under a list row
// on the layer above a surface is the single most ordinary thing this palette
// will be asked to draw, and it is the one place it does not hold.
//
// Pinned rather than fixed, for the same reason as the accents: these are the
// owner's colours. What this does is make the number a fact with a test around
// it, so that a palette edit either clears it or is told it did not.
void test_muted_text_fails_on_a_raised_card_in_daylight()
{
    const auto against = [](ColorRole ink, ColorRole page, Theme theme) {
        return contrast_ratio_centi(*color(ink, theme), *color(page, theme));
    };

    CHECK(against(ColorRole::TextMuted, ColorRole::BackgroundPrimary, Theme::Day) == 562);
    CHECK(against(ColorRole::TextMuted, ColorRole::BackgroundSurface, Theme::Day) == 495);
    CHECK(against(ColorRole::TextMuted, ColorRole::BackgroundRaised, Theme::Day) == 444);
    CHECK(against(ColorRole::TextMuted, ColorRole::BackgroundRaised, Theme::Day) <
          kContrastBodyText);

    // Primary text is fine everywhere, so the remedy is available and local:
    // muted text does not go on a raised card, or the card is not raised.
    CHECK(against(ColorRole::TextPrimary, ColorRole::BackgroundRaised, Theme::Day) >=
          kContrastBodyText);

    // Night has no raised layer to fail on — §3.1 records the gap — and every
    // foreground still clears the graphic threshold on a night card, which is
    // the tightest surface either theme has.
    for (ColorRole role : kAllRoles) {
        if (kind_of(role) != ColorKind::Foreground || !color(role, Theme::Night)) {
            continue;
        }
        const std::uint16_t on_card = against(role, ColorRole::BackgroundSurface, Theme::Night);
        if (on_card < kContrastLargeOrGraphic) {
            std::fprintf(stderr, "FAIL: %s is %u:100 on a night card\n", name_of(role), on_card);
            ++failures;
        }
    }
    CHECK(against(ColorRole::Success, ColorRole::BackgroundSurface, Theme::Night) == 354);
}

// OD-16 / issue #52's option 4: the day theme on a `PerPixel` panel resolves
// against its own column rather than the near-white one above. `Theme::Night`
// takes no `pixel_cost` argument at all — it is unaffected on either panel
// technology, so there is nothing to test there.
void test_day_is_unaffected_by_pixel_cost_on_a_fixed_panel()
{
    for (ColorRole role : kAllRoles) {
        CHECK(color(role, Theme::Day) == color(role, Theme::Day, PixelCost::Fixed));
        CHECK(is_defined_for(role, Theme::Day) == is_defined_for(role, Theme::Day, PixelCost::Fixed));
    }
}

// The emissive day palette shares its page and its ink with night's — both
// audited already — with one deliberate exception: `AccentPrimary` keeps
// Day's own Attadipa Orange, which is the entire difference left between day
// and night once both sit on a dark canvas. A palette where that difference
// disappeared would make the theme toggle silently do nothing on this panel.
void test_day_on_an_emissive_panel_is_nights_page_with_days_own_accent()
{
    const PixelCost emissive = PixelCost::PerPixel;

    CHECK(*color(ColorRole::BackgroundPrimary, Theme::Day, emissive) ==
          *color(ColorRole::BackgroundPrimary, Theme::Night));
    CHECK(*color(ColorRole::BackgroundSurface, Theme::Day, emissive) ==
          *color(ColorRole::BackgroundSurface, Theme::Night));
    CHECK(*color(ColorRole::TextPrimary, Theme::Day, emissive) ==
          *color(ColorRole::TextPrimary, Theme::Night));
    CHECK(*color(ColorRole::TextMuted, Theme::Day, emissive) ==
          *color(ColorRole::TextMuted, Theme::Night));

    // The one deliberate divergence: Day's own accent survives, Night's does not.
    CHECK(*color(ColorRole::AccentPrimary, Theme::Day, emissive) ==
          *color(ColorRole::AccentPrimary, Theme::Day, PixelCost::Fixed));
    CHECK(*color(ColorRole::AccentPrimary, Theme::Day, emissive) !=
          *color(ColorRole::AccentPrimary, Theme::Night));

    // The gap this theme was never going to fill either: no raised background
    // is defined for it, matching the same gap night already has.
    CHECK(!color(ColorRole::BackgroundRaised, Theme::Day, emissive).has_value());
    CHECK(!color(ColorRole::Danger, Theme::Day, emissive).has_value());
}

// The contrast audit §3.1 asked for. Every foreground the emissive day theme
// defines clears at least the graphic threshold on its own page — the same
// property night's table already holds, which this table borrows most of.
void test_the_emissive_day_palette_clears_the_graphic_threshold()
{
    const PixelCost emissive = PixelCost::PerPixel;
    for (ColorRole role : kAllRoles) {
        if (kind_of(role) != ColorKind::Foreground ||
            !color(role, Theme::Day, emissive).has_value()) {
            continue;   // Danger: UNKNOWN here too, on purpose
        }
        if (!legible_as_graphic(role, Theme::Day, emissive)) {
            std::fprintf(stderr, "FAIL: %s is %u:100 on the emissive day page\n", name_of(role),
                         contrast_against_page_centi(role, Theme::Day, emissive));
            ++failures;
        }
    }

    // The measured values, to a hundredth. Unlike the Fixed-panel day table,
    // AccentPrimary now clears *body text* too (5.08:1) — Attadipa Orange was
    // never illegible, it was only ever illegible on Warm Ivory.
    CHECK(contrast_against_page_centi(ColorRole::AccentPrimary, Theme::Day, emissive) == 508);
    CHECK(legible_as_body_text(ColorRole::AccentPrimary, Theme::Day, emissive));
    CHECK(contrast_against_page_centi(ColorRole::TextPrimary, Theme::Day, emissive) == 1110);

    // Its tightest case, same role and same number as night's, because it is
    // night's own value: Success on the page, not enough for a word.
    CHECK(contrast_against_page_centi(ColorRole::Success, Theme::Day, emissive) == 396);
    CHECK(!legible_as_body_text(ColorRole::Success, Theme::Day, emissive));
}

void test_the_contrast_maths_is_the_standard_one()
{
    const Rgb white{0xFF, 0xFF, 0xFF};
    const Rgb black{0x00, 0x00, 0x00};
    CHECK(contrast_ratio_centi(white, black) == 2100);   // the WCAG maximum, 21:1
    CHECK(contrast_ratio_centi(white, white) == 100);
    CHECK(contrast_ratio_centi(black, white) == 2100);   // symmetric
}

void test_a_packed_colour_round_trips()
{
    const Rgb amber = *color(ColorRole::AccentGlow, Theme::Day);
    CHECK(amber.packed() == 0xFFC857u);
    // Braced init cannot go inside CHECK — the commas are macro arguments.
    const Rgb ink{0x2F, 0x3A, 0x2E};
    CHECK(ink.packed() == 0x2F3A2Eu);
}

// ---------------------------------------------------------------------------

// Every token can say its own name, the names are the ones the design document
// uses, and none of them is the fallback. A token that prints as
// "space.unknown" in a diagnostic is a token somebody added to the enum and
// nowhere else.
void test_everything_has_a_name()
{
    std::vector<std::string> seen;
    const auto               record = [&seen](const char* n) {
        const std::string s{n};
        for (const std::string& other : seen) {
            if (other == s) {
                std::fprintf(stderr, "FAIL: duplicate token name %s\n", n);
                ++failures;
            }
        }
        if (s.find("unknown") != std::string::npos) {
            std::fprintf(stderr, "FAIL: a token fell through to %s\n", n);
            ++failures;
        }
        seen.push_back(s);
    };

    for (ColorRole r : kAllRoles) { record(name_of(r)); }
    for (PixelCost p : {PixelCost::Fixed, PixelCost::PerPixel}) { record(name_of(p)); }
    for (Space s : kAllSpace) { record(name_of(s)); }
    for (Radius r : kAllRadius) { record(name_of(r)); }
    for (Motion m : {Motion::Instant, Motion::Fast, Motion::Base, Motion::Slow}) {
        record(name_of(m));
    }
    for (Easing e : {Easing::Standard, Easing::Enter, Easing::Exit}) { record(name_of(e)); }
    for (IconSize s : kAllIcon) { record(name_of(s)); }
    for (ImageSize s : kAllImage) { record(name_of(s)); }
    for (TouchTarget t : kAllTouch) { record(name_of(t)); }
    for (Elevation e : {Elevation::Flat, Elevation::Raised, Elevation::Overlay}) {
        record(name_of(e));
    }
    for (TypeRole r : {TypeRole::Display, TypeRole::Title, TypeRole::Body, TypeRole::Label,
                       TypeRole::Caption, TypeRole::MonoDiag}) {
        record(name_of(r));
    }
    for (Haptic h : {Haptic::Tap, Haptic::Success, Haptic::Warning, Haptic::Message,
                     Haptic::Navigation, Haptic::Error, Haptic::Sos}) {
        record(name_of(h));
    }
    for (SoundCategory c : {SoundCategory::System, SoundCategory::Notifications,
                            SoundCategory::Mesh, SoundCategory::Alarms,
                            SoundCategory::Navigation}) {
        record(name_of(c));
    }

    CHECK(seen.size() == 12 + 2 + 6 + 4 + 4 + 3 + 4 + 4 + 2 + 3 + 6 + 7 + 5);
}

// An unscaled Metrics is the identity and admits it, so that a screen drawn
// before any panel has spoken is unscaled rather than accidentally correct.
void test_unscaled_metrics_say_what_they_are()
{
    const Metrics none = Metrics::unscaled();
    CHECK(!none.scaled());
    CHECK(none.px(Dp{8}) == 8);
    CHECK(none.px(dp_of(TouchTarget::Adult)) == 44);

    // A panel that could not compute a dpi returns 0 from DisplayInfo::dpi().
    // Accepting it would make an unknown panel look like a reference one, so it
    // is refused and the caller gets the identity, which is at least honest.
    CHECK(!Metrics::for_dpi(0).scaled());
    CHECK(Metrics::for_dpi(kTWatchDpi).scaled());
}

}  // namespace

int main()
{
    test_one_token_is_one_physical_size_on_both_panels();
    test_no_spacing_collapses_to_nothing();
    test_a_touch_target_is_physical_and_clears_the_minimum();
    test_pill_is_a_rule_and_not_a_length();
    test_motion_is_not_scaled_by_density();

    test_danger_is_unknown_in_both_themes();
    test_a_background_never_falls_through_to_day();
    test_every_night_foreground_survives_the_night_page();
    test_the_day_accents_cannot_carry_meaning_alone();
    test_muted_text_fails_on_a_raised_card_in_daylight();
    test_day_is_unaffected_by_pixel_cost_on_a_fixed_panel();
    test_day_on_an_emissive_panel_is_nights_page_with_days_own_accent();
    test_the_emissive_day_palette_clears_the_graphic_threshold();
    test_the_contrast_maths_is_the_standard_one();
    test_a_packed_colour_round_trips();

    test_everything_has_a_name();
    test_unscaled_metrics_say_what_they_are();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("ui tokens: all checks passed (host only — no value has been shown on a panel)\n");
    return 0;
}
