#pragma once

#include <cstdint>

#include "attadipa/apps/app_manifest.h"
#include "attadipa/core/location_service.h"
#include "attadipa/core/position.h"
#include "attadipa/l10n/locale.h"

// Where the other node is, from here.
//
// Two positions in, one readout out. Pure and host-testable, exactly as
// `clock.h` is: no LVGL, no clock read, no provider. The screen that draws it
// owns the pixels and this owns what is true.
//
// It takes two `core::LocationState`s rather than one fused position, because
// this device has two sources that are not alike and must not be blended:
// **own** position comes from a receiver on this body, **target** position is
// what a node said about itself. ADR-0016's sibling rule for power applies to
// meaning as well — a fusion engine here would be an abstraction with two
// inputs and one consumer, and it would have to invent a provenance for the
// answer it produced.

namespace attadipa::apps {

// Eight sentences, and each of them is a different thing being wrong.
//
// The owner's brief named six. `OwnPositionStale` and `OwnPositionDegraded` are
// the other two and neither is scope creep: `NoFix != stale != current !=
// unknown` is the brief's own rule, and `core::PositionValidity` already
// separates a fix that has aged from one that solved on too few satellites or
// too wide an error. Folding either into `Ready` would report a caveat the
// interface must show (`core/include/attadipa/core/position.h:188` —
// "    Degraded,  // usable, with a caveat the interface must show") as though
// there were none.
enum class NavStatus : std::uint8_t {
    WaitingForGps,        // no usable local position has arrived yet
    NoFix,                // a receiver answered, and the answer is that it cannot solve
    OwnPositionStale,     // there was a local fix and it is too old to act on
    OwnPositionDegraded,  // there is a current local fix and it solved badly
    NodeUnavailable,      // the link to the node is not up
    NodePositionUnknown,  // the node is reachable and has stated no coordinate
    NodePositionStale,    // it stated one, long enough ago that the age is the story
    Ready,                // both positions are usable
};

struct NavState {
    core::LocationState own{};     // this device's own receiver
    core::LocationState target{};  // what the node said about itself

    // How old a target coordinate may be before the readout leads with its age.
    //
    // Policy, not physics, which is why it is here rather than a constant — the
    // same argument `core::ValidityPolicy` makes. Two minutes is `ESTIMATED`:
    // a node that is not moving is still where it said it was, and one that is
    // moving invalidates itself faster than any number written here.
    //
    // It exists because the target's *validity* cannot carry this. A MeshCore
    // node states no fix type, so `classify()` correctly answers `NoFix`
    // forever, and a readout that waited for `Stale` to appear there would wait
    // for something that never comes.
    core::Millis target_stale_after{120000};

    // Every sentence this readout says comes out of `l10n/strings.toml`, so the
    // locale has to arrive with the state. It sits here rather than in the
    // face's config because the words are chosen where the meaning is.
    l10n::Locale locale{l10n::Locale::En};
};

// What the screen draws. Every field is already a string, and a field that has
// no answer is an em dash rather than a zero — `0 m` and `000°` are the two
// lies this readout exists to not tell.
struct NavText {
    char title[16]    = "";
    char distance[16] = "—";
    char bearing[8]   = "—";
    char cardinal[8]  = "";
    char status[48]   = "";
    // The letter on top of the compass ring. Here rather than in the face
    // because it is a translated word, and the face draws, it does not choose.
    char north[8]     = "";
    // What is not known, whenever something is not — including in `Ready`,
    // because a node coordinate arrives with no fix type and no observation
    // time and that stays true on the good days.
    // 96, not 64: Cyrillic is two bytes a character in UTF-8 and the longest
    // Russian caveat is 94 bytes with its age filled in. A field sized for the
    // English sentence cuts the Russian one mid-word, which is what the first
    // render of this face at `--locale ru` did.
    char caveat[96]   = "";

    NavStatus     status_code     = NavStatus::WaitingForGps;
    std::uint16_t bearing_centideg = 0;

    // The screen draws its needle from this and from nothing else. A needle
    // pointing at a default is the same lie as `000°` with a nicer typeface.
    bool has_bearing  = false;
    bool has_distance = false;
};

NavText format_navigation(const NavState &state);

// Eight points, from a bearing in centidegrees. North at 0, and each sector is
// 45° wide centred on its own point.
const char *cardinal_of(std::uint16_t bearing_centideg,
                        l10n::Locale locale = l10n::Locale::En);

const char *to_string(NavStatus status,
                      l10n::Locale locale = l10n::Locale::En);

const AppManifest &navigation_manifest();

}  // namespace attadipa::apps
