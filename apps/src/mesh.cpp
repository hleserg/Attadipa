#include "attadipa/apps/mesh.h"

#include <cstdio>
#include <cstring>

#include "attadipa/l10n/tr.h"

namespace attadipa::apps {
namespace {

using core::Availability;
using core::MeshDelivery;
using core::MeshStatus;
using core::TransportPhase;
using l10n::StringId;

void put(char* out, std::size_t size, const char* text) {
    std::snprintf(out, size, "%s", text);
}

// Whether the link has reached a verdict that changing node cannot revise.
//
// Both availabilities and the phase mean the same thing to a wearer: this link
// is not coming back by waiting, and not by picking a different node either.
bool is_terminal(const MeshStatus& status) {
    return status.availability == Availability::Failed ||
           status.availability == Availability::Incompatible ||
           status.transport == TransportPhase::Faulted;
}

// What shape the link is in.
//
// A REFUSAL OUTRANKS THE PHASE, because it explains the phase. A watch that
// turned the only node in range away has no session at all, so the phase is
// some flavour of "nothing here" and every session-scoped field is empty --
// exactly when the operator most needs to know that the silence is deliberate.
// `core/include/attadipa/core/mesh_service.h:70` — "    // WHICH NODE THIS WATCH IS PINNED TO, AND THE LAST ONE IT TURNED AWAY."
//
// BUT NOT OVER A TERMINAL ONE, because there the refusal is history and the
// fault is now. `MeshCoreCompanion::reset_session()` deliberately keeps the pin
// and the refusal across a disconnect
// (`link/src/meshcore_companion.cpp:161` — "    // `status_.pinned_id` and `status_.refused_id` are deliberately NOT cleared"),
// which is right, and it means a refusal latched at any point in the past is
// still latched after the transport later faults. Ranked first, it answered
// `TurnedAway` there -- telling the wearer to go and select a different node,
// which is an instruction that cannot clear a fault, in place of the `Broken`
// note that says a reset is what this needs. The refusal keeps its precedence
// over every non-terminal phase, which is where it is still the explanation
// for the silence.
//
// Availability is consulted before the transport for the two answers the
// transport cannot give: a device that can never carry a mesh, and one that
// could but has been told about no node. Both leave the transport at `Absent`,
// and "no radio" and "no node named" are different things to do about it.
MeshLink link_of(const MeshStatus& status) {
    if (status.has_refused && status.has_pinned && !is_terminal(status)) {
        return MeshLink::TurnedAway;
    }
    switch (status.availability) {
    case Availability::Unsupported:
        return MeshLink::NoRadio;
    case Availability::Unprovisioned:
        return MeshLink::NoNode;
    case Availability::Incompatible:
    case Availability::Failed:
        return MeshLink::Broken;
    case Availability::Off:
        return MeshLink::Resting;
    case Availability::Unreachable:
    case Availability::Ready:
        break;  // the transport carries the detail
    }
    switch (status.transport) {
    case TransportPhase::Absent:
        return MeshLink::NoRadio;
    case TransportPhase::Attached:
        return MeshLink::Silent;
    case TransportPhase::Connecting:
        return MeshLink::Reaching;
    case TransportPhase::Ready:
        return MeshLink::Linked;
    case TransportPhase::Suspended:
        return MeshLink::Resting;
    case TransportPhase::Faulted:
        return MeshLink::Broken;
    }
    return MeshLink::NoRadio;
}

StringId state_word(MeshLink link) {
    switch (link) {
    case MeshLink::NoNode:     return StringId::MeshStateNoNode;
    case MeshLink::NoRadio:    return StringId::MeshStateNoRadio;
    case MeshLink::Silent:     return StringId::MeshStateSilent;
    case MeshLink::Reaching:   return StringId::MeshStateReaching;
    case MeshLink::Linked:     return StringId::MeshStateLinked;
    case MeshLink::Resting:    return StringId::MeshStateResting;
    case MeshLink::Broken:     return StringId::MeshStateBroken;
    case MeshLink::TurnedAway: return StringId::MeshStateTurnedAway;
    }
    return StringId::MeshStateNoRadio;
}

// `Linked` has no note. Its state word says everything the note would, and a
// sentence under it would be there only to fill the space.
const char* note_for(MeshLink link, l10n::Locale locale) {
    switch (link) {
    case MeshLink::NoNode:     return l10n::tr(StringId::MeshNoteNoNode, locale);
    case MeshLink::NoRadio:    return l10n::tr(StringId::MeshNoteNoRadio, locale);
    case MeshLink::Silent:     return l10n::tr(StringId::MeshNoteSilent, locale);
    case MeshLink::Reaching:   return l10n::tr(StringId::MeshNoteReaching, locale);
    case MeshLink::Resting:    return l10n::tr(StringId::MeshNoteResting, locale);
    case MeshLink::Broken:     return l10n::tr(StringId::MeshNoteBroken, locale);
    case MeshLink::TurnedAway: return l10n::tr(StringId::MeshNoteTurnedAway, locale);
    case MeshLink::Linked:     break;
    }
    return "";
}

// How many of the five segments carry the state's colour. It is not a progress
// bar: `Broken` lights all five *and* the face draws a gap through them, which
// is the difference between a link that is partly made and one that is severed.
std::uint8_t rungs_for(MeshLink link) {
    switch (link) {
    case MeshLink::Linked:
    case MeshLink::Resting:
    case MeshLink::Broken:
        return kChannelRungs;
    case MeshLink::Reaching:
        return 2;
    case MeshLink::NoNode:
    case MeshLink::NoRadio:
    case MeshLink::Silent:
    case MeshLink::TurnedAway:
        return 0;
    }
    return 0;
}

StringId delivery_word(MeshDelivery delivery) {
    switch (delivery) {
    case MeshDelivery::None:      return StringId::MeshDeliveryNone;
    case MeshDelivery::Queued:    return StringId::MeshDeliveryQueued;
    case MeshDelivery::Accepted:  return StringId::MeshDeliveryAccepted;
    case MeshDelivery::Confirmed: return StringId::MeshDeliveryConfirmed;
    case MeshDelivery::Failed:    return StringId::MeshDeliveryFailed;
    }
    return StringId::MeshDeliveryNone;
}

}  // namespace

void mesh_key_prefix(const core::MeshPeerId& id, char (&out)[9]) {
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 4; ++i) {
        out[i * 2]     = kHex[id.public_key[i] >> 4U];
        out[i * 2 + 1] = kHex[id.public_key[i] & 0x0FU];
    }
    out[8] = '\0';
}

MeshText format_mesh(const MeshStatus& status, l10n::Locale locale)
{
    MeshText text{};
    text.link      = link_of(status);
    text.rungs_lit = rungs_for(text.link);

    put(text.title, sizeof(text.title), l10n::tr(StringId::MeshTitle, locale));
    put(text.state, sizeof(text.state), l10n::tr(state_word(text.link), locale));
    put(text.note, sizeof(text.note), note_for(text.link, locale));

    // The way out is offered only where holding the clock actually leads
    // somewhere. A watch with no mesh radio will not grow one, and an
    // instruction that does nothing is worse than no instruction.
    if (text.link == MeshLink::NoNode) {
        put(text.way_out, sizeof(text.way_out),
            l10n::tr(StringId::MeshWayOutNameNode, locale));
    } else if (text.link == MeshLink::TurnedAway) {
        put(text.way_out, sizeof(text.way_out),
            l10n::tr(StringId::MeshWayOutChangeNode, locale));
    }

    text.has_node = status.has_node_id;
    if (status.has_node_id) {
        char hex[9];
        mesh_key_prefix(status.node_id, hex);
        put(text.node_key, sizeof(text.node_key), hex);
    }
    if (status.node_name[0] != '\0') {
        put(text.node_name, sizeof(text.node_name), status.node_name.data());
    }

    // A latched refusal, not the screen that usually reports one. `TurnedAway`
    // is the answer only while the transport is non-terminal; once it faults,
    // `link_of()` ranks the fault first and the screen becomes `Broken`. The
    // refusal is still latched there -- `reset_session()` keeps the pin and the
    // refusal across a disconnect on purpose -- so the reset that screen asks
    // for clears the fault and leaves the refusal. Withholding the two keys
    // exactly there took the only pointer to the entry screen's node field off
    // the one screen whose recommended action does not lead out of it.
    if (status.has_refused && status.has_pinned) {
        char want[9];
        char bad[9];
        mesh_key_prefix(status.pinned_id, want);
        mesh_key_prefix(status.refused_id, bad);
        std::snprintf(text.pinned, sizeof(text.pinned),
                      l10n::tr(StringId::MeshPinned, locale), want);
        std::snprintf(text.answered, sizeof(text.answered),
                      l10n::tr(StringId::MeshAnswered, locale), bad);
    }

    // The message block and the measurements exist only where there is a link
    // to have heard something over. Everywhere else nothing is drawn -- not an
    // empty heading, not `peers 0`, not an em dash standing in for a number.
    const bool has_link =
        text.link == MeshLink::Linked || text.link == MeshLink::Resting;
    text.live        = text.link == MeshLink::Linked;
    text.has_message = has_link;
    text.has_signal  = has_link;

    if (has_link) {
        // A message the provider cut is only a claim about a message there is.
        // With nothing heard, `last_message` is empty and the retained flag is
        // stale evidence about some earlier one, so it says nothing here.
        text.message_partial =
            status.message_truncated && status.last_message[0] != '\0';
        const StringId heading =
            text.live ? (text.message_partial ? StringId::MeshMessageHeadingCut
                                              : StringId::MeshMessageHeading)
                      : (text.message_partial ? StringId::MeshMessageLastKnownCut
                                              : StringId::MeshMessageLastKnown);
        put(text.message_heading, sizeof(text.message_heading),
            l10n::tr(heading, locale));
        put(text.message, sizeof(text.message),
            status.last_message[0] != '\0'
                ? status.last_message.data()
                : l10n::tr(StringId::MeshNoMessage, locale));
        if (status.last_sender[0] != '\0') {
            put(text.sender, sizeof(text.sender), status.last_sender.data());
        }
        put(text.delivery, sizeof(text.delivery),
            l10n::tr(delivery_word(status.delivery), locale));

        put(text.peers_label, sizeof(text.peers_label),
            l10n::tr(StringId::MeshLabelPeers, locale));
        // KEPT OF REPORTED, AND ONLY WHERE THOSE ARE DIFFERENT NUMBERS.
        //
        // `peers_reported` is the node's own count and is honest on its own;
        // what the retained cap costs is that a sender past it cannot be
        // named, because `find_peer_prefix()` has nothing to match. One number
        // where the watch kept them all reads as "there are this many"; two
        // where it did not reads as "there are this many and I have that many
        // of them", which is the only difference this face is in a position to
        // make. It needs no word, so it needs no translation.
        //
        // WHAT THE GATE IS NOT: a truncation flag. That flag had one writer,
        // the seventeenth distinct contact frame, and the far commoner way to
        // keep fewer than the node reports never reached it -- a contact whose
        // advert type is not chat is dropped before any count moves, and the
        // project's own bench node has two of them. Gated on truncation this
        // face printed one number on exactly the list the pair exists for.
        //
        // What it is instead is `peers_complete`, because the two numbers are
        // only comparable once the node's iteration has ended: mid-sync the
        // retained count climbs from zero against a total that is already
        // final, and the pair would count up through `3/40`. Both numbers also
        // have to be present AND different -- `16/16` says the difference this
        // face exists to make and then denies it, and `16/5` says the watch
        // kept more than the node has.
        // And the pair means "I have this many of those", so it is printed
        // only where the watch actually kept FEWER than the node claims.
        // `16/16` states the difference and denies it in the same breath;
        // `16/5` is worse, because it says the watch holds more peers than
        // exist. Everywhere else one number -- and the larger of the two, not
        // the node's. A node that reported 5 and then sent twenty distinct
        // contact frames has a stale count, and printing it would put a number
        // on this face smaller than the set the watch can name a sender from.
        // Outside that case the two agree or the node's is the larger, so this
        // stays the reported count everywhere it already was.
        const auto retained = static_cast<unsigned>(status.peers_retained);
        const auto reported = static_cast<unsigned>(status.peers_reported);
        if (status.peers_complete && retained < reported) {
            std::snprintf(text.peers, sizeof(text.peers), "%u/%u", retained,
                          reported);
        } else {
            std::snprintf(text.peers, sizeof(text.peers), "%u",
                          retained > reported ? retained : reported);
        }

        text.has_snr = status.has_snr;
        if (status.has_snr) {
            // Quarter-dB units, printed as two decimals because a quarter is
            // .25 exactly and rounding it to one would make 7.25 and 7.5 the
            // same number on screen.
            const int magnitude = status.snr_quarter_db < 0
                                      ? -static_cast<int>(status.snr_quarter_db)
                                      : static_cast<int>(status.snr_quarter_db);
            std::snprintf(text.snr, sizeof(text.snr), "%s%d.%02d",
                          status.snr_quarter_db < 0 ? "-" : "",
                          magnitude / 4, (magnitude % 4) * 25);
            put(text.snr_label, sizeof(text.snr_label),
                l10n::tr(StringId::MeshLabelSnr, locale));
        }

        // An MTU of zero is one that was never negotiated, not a link that
        // carries nothing, so it is absent rather than printed as 0.
        text.has_mtu = status.mtu != 0;
        if (text.has_mtu) {
            std::snprintf(text.mtu, sizeof(text.mtu), "%u",
                          static_cast<unsigned>(status.mtu));
            put(text.mtu_label, sizeof(text.mtu_label),
                l10n::tr(StringId::MeshLabelMtu, locale));
        }
    }
    put(text.watch_power, sizeof(text.watch_power),
        l10n::tr(StringId::StatusWatchUnknown, locale));
    const auto& battery = status.node_battery;
    if (battery.separate_supply) {
        const bool known = status.has_node_id && battery.millivolts != 0 &&
            (battery.validity == core::Validity::Valid ||
             battery.validity == core::Validity::Stale);
        if (known && text.link == MeshLink::Linked &&
            battery.validity == core::Validity::Valid) {
            std::snprintf(text.node_power, sizeof(text.node_power),
                          l10n::tr(StringId::StatusNodeVoltage, locale),
                          static_cast<unsigned>(battery.millivolts / 1000U),
                          static_cast<unsigned>(battery.millivolts % 1000U));
        } else {
            put(text.node_power, sizeof(text.node_power),
                l10n::tr(known ? StringId::StatusNodeStale
                               : StringId::StatusNodeUnknown, locale));
        }
    }
    return text;
}

}  // namespace attadipa::apps
