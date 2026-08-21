#include "attadipa/platform/hardware_inventory.h"

namespace attadipa::platform {

ProfileInventory::ProfileInventory(const BoardProfile& profile) : profile_(&profile)
{
    for (std::uint8_t i = 0; i < kHardwareFeatureCount; ++i) {
        const auto feature = static_cast<HardwareFeature>(i);
        // Present but untouched is the correct starting state, and it is the
        // reason HardwareState has an Untouched value at all. Nothing has been
        // brought up yet, and "not brought up" must not read as "broken"
        // (final §32).
        states_[i] = profile.present(feature) ? HardwareState::Untouched : HardwareState::Absent;
    }
}

bool ProfileInventory::present(HardwareFeature feature) const
{
    return profile_->present(feature);
}

HardwareState ProfileInventory::state(HardwareFeature feature) const
{
    const auto index = static_cast<std::uint8_t>(feature);
    return index < kHardwareFeatureCount ? states_[index] : HardwareState::Absent;
}

const RadioInfo* ProfileInventory::radio() const
{
    return present(HardwareFeature::Radio) ? &profile_->radio : nullptr;
}

void ProfileInventory::set_state(HardwareFeature feature, HardwareState state)
{
    const auto index = static_cast<std::uint8_t>(feature);
    if (index >= kHardwareFeatureCount || !profile_->present(feature)) {
        return;  // a part that is not there cannot fail, and must not appear to
    }
    states_[index] = state;
}

}  // namespace attadipa::platform
