#pragma once

#include <cstdint>

#include "attadipa/core/mesh_service.h"
#include "attadipa/l10n/locale.h"

// What the mesh link is, in words a wearer can act on.
//
// One `core::MeshStatus` in, one readout out. Pure and host-testable, exactly
// as `navigation.h` is: no LVGL, no provider, no board. The screen that draws
// it owns the pixels and this owns what is true.
//
// This exists because the screen used to print `core::to_string(transport)`
// straight onto the panel. `TransportPhase::Attached` is a true statement about
// a peripheral and a useless one on a wrist -- it does not tell a wearer
// whether to keep walking or to go and fix something, and half of the words it
// can produce are enumerator names no operator has ever been taught.

namespace attadipa::apps {

// The shape the link is in. Eight, and each is a different thing to do about
// it -- which is why `Absent` and `Unsupported` share one and `Ready` does not
// share with anything.
enum class MeshLink : std::uint8_t {
    NoNode,      // no node is named; nothing is being reached for
    NoRadio,     // this device cannot carry a mesh at all, or the radio is gone
    Silent,      // the radio is up and nobody is on it
    Reaching,    // a peer is arriving -- advertising, enumerating, handshaking
    Linked,      // a peer has been heard from inside the liveness window
    Resting,     // deliberately quiesced; what is shown is history, not now
    Broken,      // it failed, and needs a reset rather than a retry
    TurnedAway,  // a node answered and it was not the one this watch is pinned to
};

// A `MeshLink` and the strings that go with it.
//
// Every field is either filled or empty, and empty means "do not draw this",
// never "draw a zero". A screen that prints `peers 0` for a radio that is not
// there is the same lie as `0 m` on the navigation face, told about a different
// unknown -- so `has_signal` gates the whole measurement row rather than each
// value carrying a sentinel.
struct MeshText {
    char title[16]   = "";
    char state[24]   = "";
    char note[72]    = "";
    // What to do about it, when there is anything to do. Empty for every state
    // the wearer cannot leave from here: a watch with no mesh radio is not
    // going to grow one, and telling it to hold the clock would be an
    // instruction that does nothing.
    char way_out[64] = "";

    char node_key[12]  = "";
    char node_name[40] = "";
    // Pre-formatted, label included, because which word introduces a key is a
    // translation decision and this is the layer that makes those.
    char pinned[40]   = "";
    char answered[40] = "";

    // 48 rather than 24 because the heading is also where the message block
    // says its content is incomplete, and `СООБЩЕНИЕ · ОБРЕЗАНО` is 38 bytes.
    char message_heading[48] = "";
    // The whole of what came over the link, not as much of it as an earlier
    // guess had room for. `core::MeshStatus::last_message` carries
    // `kMeshTextBytes`, and a smaller buffer here threw the tail away before
    // anything had asked whether it fits -- silently, because `put()` is
    // `snprintf`, and at a byte rather than at a character, so a cut landing
    // between the two bytes of a Cyrillic code point put half a character on
    // the panel. What does not fit is now ellipsised where it is drawn, by the
    // face, which is the layer that knows how wide the panel is.
    char message[core::kMeshTextBytes + 1] = "";
    char sender[40]          = "";
    // 32 rather than 24 because `не отправлено` and `не доставлено` are 25
    // bytes each and lost their last character -- and `MeshDelivery::None` is
    // the default, so the truncated word was the one a watch showed first.
    char delivery[32]        = "";

    char snr[12]         = "";
    char snr_label[16]   = "";
    // 16 rather than 8 because a watch that kept fewer peers than the node
    // reported prints both numbers -- `16/65535` is the widest that can be.
    char peers[16]       = "";
    char peers_label[16] = "";
    char mtu[8]          = "";
    char mtu_label[16]   = "";

    MeshLink link = MeshLink::NoNode;

    // How much of the channel between this watch and the node is drawn lit,
    // out of `kChannelRungs`. It is here rather than in the face because it is
    // the same statement as `state`, and two places deciding it is two places
    // to disagree the next time a phase is added.
    std::uint8_t rungs_lit = 0;

    bool has_node    = false;
    bool has_message = false;
    // Whether the message block is current. `Resting` fills the same three
    // fields as `Linked` and must not look the same doing it: there is no age
    // in `core::MeshStatus` to print, so the honest claim is not "3 minutes
    // ago" but that this is the last thing heard rather than what is arriving.
    bool live        = false;
    bool has_signal  = false;
    bool has_snr     = false;
    bool has_mtu     = false;

    // WHAT THE PROVIDER THREW AWAY BEFORE THIS LAYER SAW IT.
    //
    // Neither of these is a layout problem, and that is the whole point of
    // carrying them. `message_partial` is a tail the node sent and the watch
    // no longer has: `MeshCoreCompanion::accept_message()` copies into
    // `core::MeshStatus::last_message` and reports the overflow
    // (`core/include/attadipa/core/mesh_service.h:96` — "    bool message_truncated = false;").
    // The face's one-line ellipsis is a different statement -- "the rest of
    // this is off the edge of a 240 px panel" -- and it is recoverable by
    // definition, because the bytes are still in `message`. A wearer reading
    // dots cannot tell the two apart, so the words say which it is.
    //
    // The peer cap is the same shape one field over and gets no flag, because
    // it needs none: the watch retains a fixed 16 contacts, the node may have
    // more, and the whole treatment is that `peers` reads `16/40` instead of
    // `40`. The string is the cue. A parallel bool that no renderer reads is a
    // second way to ask the same question and a second thing to keep true.
    bool message_partial = false;

    // Shared active-screen status. No watch battery producer is bound yet.
    // Empty node_power means one integrated supply; unknown is a visible word.
    char watch_power[24] = "";
    char node_power[40] = "";
};

// How many segments the channel is drawn in. Five, because the lit count has to
// read as a fraction at a glance on a 240 px panel: fewer cannot show "part of
// the way" at all, more and the segments stop being separable.
inline constexpr std::uint8_t kChannelRungs = 5;

// Four bytes of a node's public key as hex, into a nine-byte buffer.
//
// A name is not an identity. The bench ran two MeshCore nodes whose names
// differed by an emoji and whose advertisements are interchangeable, and the
// bench reports identify nodes by exactly this much (`5c62d9bc…`, `044e2de8…`)
// -- `docs/research/MESHCORE_T114_FIRST_CONTACT.md:63` — "public key".
void mesh_key_prefix(const core::MeshPeerId& id, char (&out)[9]);

MeshText format_mesh(const core::MeshStatus& status, l10n::Locale locale);

}  // namespace attadipa::apps
