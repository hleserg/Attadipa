#pragma once

#include "firefly/platform/board_profile.h"
#include "firefly/platform/display_info.h"
#include "firefly/platform/hardware_feature.h"
#include "firefly/platform/radio_info.h"

namespace firefly::platform {

// The hardware inventory: three questions and no more.
//
//   present()  is this part physically here      — a fact about a board
//   state()    is its driver up                  — changes while running
//   radio()    which part, typed                 — nullptr when none is fitted
//
// No node ever makes present() true. A Waveshare board has
// present(GnssReceiver) == false forever, and that is correct — the board has
// no GNSS receiver. Whether the *device* can report a position is a different
// question, asked of firefly::core::CapabilityRegistry in a different
// vocabulary. docs/adr/0007-two-capability-layers.md.
class HardwareInventory {
public:
    virtual ~HardwareInventory() = default;

    virtual bool          present(HardwareFeature feature) const = 0;
    virtual HardwareState state(HardwareFeature feature) const   = 0;
    virtual const RadioInfo* radio() const                       = 0;

    virtual const DisplayInfo& display() const    = 0;
    virtual const char*        board_id() const   = 0;
    virtual const char*        board_name() const = 0;
};

// An inventory built from a BoardProfile.
//
// This is what the simulator uses, and it is also the shape a BSP fills in:
// the board contributes a profile, and bring-up moves individual features from
// Untouched to Ready or Failed. Nothing here talks to a bus — a profile is a
// description, and describing a part is not initialising it (final §32).
class ProfileInventory final : public HardwareInventory {
public:
    explicit ProfileInventory(const BoardProfile& profile);

    bool             present(HardwareFeature feature) const override;
    HardwareState    state(HardwareFeature feature) const override;
    const RadioInfo* radio() const override;

    const DisplayInfo& display() const override { return profile_->display; }
    const char*        board_id() const override { return profile_->id; }
    const char*        board_name() const override { return profile_->name; }

    // Bring-up, and its opposite. Setting a state for an absent feature is
    // ignored: a part that is not there cannot fail, and pretending otherwise
    // is how "the compass is broken" ends up on a watch that never had one.
    void set_state(HardwareFeature feature, HardwareState state);

    const BoardProfile& profile() const { return *profile_; }

private:
    const BoardProfile* profile_;
    HardwareState       states_[kHardwareFeatureCount];
};

}  // namespace firefly::platform
