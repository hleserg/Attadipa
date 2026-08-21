#include "attadipa/ui/tokens.h"

namespace attadipa::ui {

const char* name_of(Space s)
{
    switch (s) {
        case Space::Xs:  return "space.xs";
        case Space::Sm:  return "space.sm";
        case Space::Md:  return "space.md";
        case Space::Lg:  return "space.lg";
        case Space::Xl:  return "space.xl";
        case Space::Xxl: return "space.xxl";
    }
    return "space.unknown";
}

const char* name_of(Radius r)
{
    switch (r) {
        case Radius::Sm:   return "radius.sm";
        case Radius::Md:   return "radius.md";
        case Radius::Lg:   return "radius.lg";
        case Radius::Pill: return "radius.pill";
    }
    return "radius.unknown";
}

const char* name_of(Motion m)
{
    switch (m) {
        case Motion::Instant: return "motion.duration.instant";
        case Motion::Fast:    return "motion.duration.fast";
        case Motion::Base:    return "motion.duration.base";
        case Motion::Slow:    return "motion.duration.slow";
    }
    return "motion.duration.unknown";
}

const char* name_of(Easing e)
{
    switch (e) {
        case Easing::Standard: return "motion.easing.standard";
        case Easing::Enter:    return "motion.easing.enter";
        case Easing::Exit:     return "motion.easing.exit";
    }
    return "motion.easing.unknown";
}

const char* name_of(IconSize s)
{
    switch (s) {
        case IconSize::Sm: return "icon.size.sm";
        case IconSize::Md: return "icon.size.md";
        case IconSize::Lg: return "icon.size.lg";
        case IconSize::Xl: return "icon.size.xl";
    }
    return "icon.size.unknown";
}

const char* name_of(ImageSize s)
{
    switch (s) {
        case ImageSize::Inline:    return "image.size.inline";
        case ImageSize::Spot:      return "image.size.spot";
        case ImageSize::Hero:      return "image.size.hero";
        case ImageSize::HeroLarge: return "image.size.hero.large";
    }
    return "image.size.unknown";
}

const char* name_of(TouchTarget t)
{
    switch (t) {
        case TouchTarget::Adult:     return "touch.min.adult";
        case TouchTarget::ChildMode: return "touch.min.child";
    }
    return "touch.min.unknown";
}

const char* name_of(Elevation e)
{
    switch (e) {
        case Elevation::Flat:    return "elevation.flat";
        case Elevation::Raised:  return "elevation.raised";
        case Elevation::Overlay: return "elevation.overlay";
    }
    return "elevation.unknown";
}

const char* name_of(TypeRole r)
{
    switch (r) {
        case TypeRole::Display:  return "type.display";
        case TypeRole::Title:    return "type.title";
        case TypeRole::Body:     return "type.body";
        case TypeRole::Label:    return "type.label";
        case TypeRole::Caption:  return "type.caption";
        case TypeRole::MonoDiag: return "type.mono.diag";
    }
    return "type.unknown";
}

const char* name_of(Haptic h)
{
    switch (h) {
        case Haptic::Tap:        return "haptic.tap";
        case Haptic::Success:    return "haptic.success";
        case Haptic::Warning:    return "haptic.warning";
        case Haptic::Message:    return "haptic.message";
        case Haptic::Navigation: return "haptic.navigation";
        case Haptic::Error:      return "haptic.error";
        case Haptic::Sos:        return "haptic.sos";
    }
    return "haptic.unknown";
}

const char* name_of(SoundCategory c)
{
    switch (c) {
        case SoundCategory::System:        return "sound.category.system";
        case SoundCategory::Notifications: return "sound.category.notifications";
        case SoundCategory::Mesh:          return "sound.category.mesh";
        case SoundCategory::Alarms:        return "sound.category.alarms";
        case SoundCategory::Navigation:    return "sound.category.navigation";
    }
    return "sound.category.unknown";
}

}  // namespace attadipa::ui
