#include "attadipa/apps/clock_screen.h"

#include <cstdio>

#include "attadipa/apps/app_manifest.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/ui/tokens.h"
#include "attadipa/ui/widgets.h"
#include "attadipa_fonts.h"

namespace attadipa::apps {
namespace {

using l10n::StringId;
using l10n::tr;
using ui::ColorRole;
using ui::Dp;
using ui::Metrics;
using ui::Space;
using ui::Theme;
using ui::TypeRole;

lv_color_t paint(ColorRole role, Theme theme);

// Text a person has to read, painted in a colour that can carry it.
//
// This is the contrast API earning its place rather than decorating a header.
// The day palette's accents measure 1.44:1 to 2.81:1 against Warm Ivory — every
// one of them below the 4.5:1 a word needs — so on the day theme an accent
// **is not used for text**, and the ink colour is used instead. At night the
// same roles clear 5:1 and the colour comes back.
//
// The alternative was a rule in a document that says "do not put orange words
// on ivory", which is a rule until somebody does it anyway. This one cannot be
// broken by writing the wrong line: `legible_as_body_text()` is arithmetic, and
// it is checked here, at the moment the colour is chosen.
lv_color_t paint_readable(ColorRole role, Theme theme)
{
    return paint(ui::legible_as_body_text(role, theme) ? role : ColorRole::TextPrimary, theme);
}

// The face resolves a colour through the theme exactly like the diagnostic
// does, and substitutes the same way when a role is UNKNOWN — there is no red
// in either owner palette, so `Danger` has no value and the screen must not
// invent one.
lv_color_t paint(ColorRole role, Theme theme)
{
    if (const std::optional<ui::Rgb> value = ui::color(role, theme)) {
        return lv_color_hex(value->packed());
    }
    const ColorRole substitute = ui::kind_of(role) == ui::ColorKind::Background
                                     ? ColorRole::BackgroundPrimary
                                     : ColorRole::TextPrimary;
    return lv_color_hex(ui::color(substitute, theme)->packed());
}

// Scaffolding, and it says so in tokens.h: `TypeRole` carries no sizes because
// final §51 will not let a face be adopted before four things are checked, and
// none has been. Until then a role picks one of the four generated Montserrat
// subsets. When the type scale exists, this function's body is replaced and no
// call site changes.
const lv_font_t* font_for(TypeRole role, const Metrics& metrics, bool child_mode)
{
    // Chosen by density, not by board name — the ui library does not know which
    // panel this is and neither does this file. A `Display` role wants roughly
    // 10 mm of cap height on any watch, which resolves to the 64 px face on the
    // T-Watch's 261 dpi and the 96 px one on the Waveshare's 315 dpi.
    const bool wide = metrics.px(Dp{160}) >= 300;

    switch (role) {
        case TypeRole::Display:
            return wide ? &attadipa_montserrat_num_96 : &attadipa_montserrat_num_64;
        case TypeRole::Title:
            return child_mode ? &attadipa_montserrat_28 : &attadipa_montserrat_20;
        case TypeRole::Body:
            return child_mode ? &attadipa_montserrat_20 : &attadipa_montserrat_16;
        default:
            return child_mode ? &attadipa_montserrat_16 : &attadipa_montserrat_14;
    }
}

StringId weekday_id(std::uint8_t weekday)
{
    static const StringId kDays[] = {
        StringId::WeekdayMon, StringId::WeekdayTue, StringId::WeekdayWed, StringId::WeekdayThu,
        StringId::WeekdayFri, StringId::WeekdaySat, StringId::WeekdaySun,
    };
    return kDays[weekday % 7];
}

StringId month_id(std::uint8_t month)
{
    static const StringId kMonths[] = {
        StringId::MonthJan, StringId::MonthFeb, StringId::MonthMar, StringId::MonthApr,
        StringId::MonthMay, StringId::MonthJun, StringId::MonthJul, StringId::MonthAug,
        StringId::MonthSep, StringId::MonthOct, StringId::MonthNov, StringId::MonthDec,
    };
    return kMonths[(month == 0 ? 0 : month - 1) % 12];
}

}  // namespace

void build_clock(lv_obj_t* parent, const ClockModel& model, const Metrics& metrics, Theme theme)
{
    const bool child = model.child_mode;

    lv_obj_clean(parent);
    lv_obj_set_style_bg_color(parent, paint(ColorRole::BackgroundPrimary, theme), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, metrics.px(ui::dp_of(Space::Lg)), 0);
    lv_obj_set_style_pad_row(parent, metrics.px(ui::dp_of(Space::Sm)), 0);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    // --- Child Mode says hello, and that is the whole difference at the top.
    // Not a different screen: the same screen, larger, with one warm line on it.
    if (child) {
        lv_obj_t* greeting = lv_label_create(parent);
        lv_label_set_text(greeting, tr(StringId::ClockChildGreeting));
        lv_obj_set_style_text_font(greeting, font_for(TypeRole::Body, metrics, child), 0);
        lv_obj_set_style_text_color(greeting, paint_readable(ColorRole::AccentPrimary, theme), 0);
    }

    // --- The time. The one thing this screen exists for.
    lv_obj_t* time_label = lv_label_create(parent);
    if (model.time_known) {
        char buffer[8];
        std::snprintf(buffer, sizeof buffer, "%02u:%02u", static_cast<unsigned>(model.hour),
                      static_cast<unsigned>(model.minute));
        lv_label_set_text(time_label, buffer);
        lv_obj_set_style_text_color(time_label, paint(ColorRole::TextPrimary, theme), 0);
    } else {
        // Not 00:00. A watch that does not know the time says it does not know
        // the time — ADR-0011's rule, in the place a person meets it first.
        lv_label_set_text(time_label, tr(StringId::ClockNoTime));
        lv_obj_set_style_text_color(time_label, paint(ColorRole::TextMuted, theme), 0);
    }
    lv_obj_set_style_text_font(time_label, font_for(TypeRole::Display, metrics, child), 0);

    // --- The date, in a form each language actually uses.
    if (model.date_known) {
        lv_obj_t* date_label = lv_label_create(parent);
        lv_label_set_text_fmt(date_label, tr(StringId::ClockDate), tr(weekday_id(model.weekday)),
                              static_cast<unsigned>(model.day), tr(month_id(model.month)));
        lv_obj_set_style_text_font(date_label, font_for(TypeRole::Body, metrics, child), 0);
        lv_obj_set_style_text_color(date_label, paint(ColorRole::TextMuted, theme), 0);
    }

    // --- Battery: a gauge that fills to the charge, and the number beside it.
    //
    // It was a bare "62%" until the owner's review on 2026-08-22 asked what the
    // percentage was of — which is the question a number with no picture always
    // gets. The widget owns both halves so that Settings cannot draw a
    // different-looking battery next week.
    ui::widgets::Battery battery;
    battery.known    = model.battery_known;
    battery.percent  = model.battery_percent;
    battery.charging = model.charging;
    battery.large    = child;
    ui::widgets::build_battery(parent, battery, metrics, theme,
                               font_for(TypeRole::Caption, metrics, child));

    if (model.charging) {
        lv_obj_t* charging_label = lv_label_create(parent);
        lv_label_set_text(charging_label, tr(StringId::ClockCharging));
        lv_obj_set_style_text_font(charging_label, font_for(TypeRole::Caption, metrics, child), 0);
        lv_obj_set_style_text_color(charging_label, paint_readable(ColorRole::Success, theme), 0);
    }

    // --- The status row, and never in Child Mode.
    //
    // "Unreachable" is not a thing a seven-year-old needs on their wrist (final
    // §58), and neither is a struck-through icon they cannot act on. The adult
    // face gets three; the child face gets a clock.
    if (!child) {
        const ui::widgets::StatusIcon icons[] = {
            {assets::Icon::Mesh, model.mesh},
            {assets::Icon::Position, model.position},
            {assets::Icon::Companion, model.phone},
        };
        ui::widgets::build_status_strip(parent, icons,
                                        sizeof icons / sizeof icons[0], metrics, theme);
    }
}

const AppManifest& clock_manifest()
{
    static const core::Capability kRequired[] = {core::Capability::Time};
    static const core::Capability kEnhanced[] = {
        core::Capability::MotionSensing,
        core::Capability::MeshMessaging,
        core::Capability::Position,
    };
    static const AppManifest kManifest{
        "clock", kRequired, 1, kEnhanced, 3,
    };
    return kManifest;
}

}  // namespace attadipa::apps
