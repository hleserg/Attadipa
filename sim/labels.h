#pragma once

#include "attadipa/core/availability.h"
#include "attadipa/core/capability_registry.h"
#include "attadipa/l10n/string_id.h"
#include "attadipa/platform/hardware_inventory.h"

namespace attadipa::sim {

// Enum values, in words a person reads.
//
// **This lives in the composition root and it has to.** `core` may not link
// `l10n` — that is the second boundary check in tests/boundary/, and it exists
// so that the core never learns which language will be read. `apps` may not link
// `platform`, which is the first check. A table mapping a `HardwareFeature` to a
// `StringId` therefore has exactly one legal home: the one place allowed to see
// both halves, which is `main()` on a device and this file in the simulator.
//
// Added 2026-08-22, after the owner looked at a Russian screenshot in which the
// only Russian word was "возможностей". The old argument — that a diagnostic is
// read by a developer, so enum identifiers are the *right* text — is a fair
// argument about a diagnostic and a bad one about the only screen that exists.
// The identifiers are still printed by `--list-boards` and on stderr, which is
// where somebody grepping for a symbol is actually looking.
l10n::StringId label_of(core::Capability capability);
l10n::StringId label_of(core::Availability availability);
l10n::StringId label_of(platform::HardwareFeature feature);
l10n::StringId label_of(platform::HardwareState state);
l10n::StringId label_of(platform::MeshCoreSupport support);

// A radio chip is a part number — SX1262, LR1121 — and a part number is not
// translated. Only "unknown" is a word, so only "unknown" comes from the
// catalogue; everything else returns nullptr and the caller prints the number.
const char* chip_name(platform::RadioChip chip);

}  // namespace attadipa::sim
