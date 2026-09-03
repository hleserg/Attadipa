// The first position owner, tested through the seam that ships.
//
// Most of what follows drives a real `MeshCoreCompanion` with real frames and
// reads the result through `NodePositionProvider` and `LocationService`,
// because the interesting claims of this slice are claims about the shipping
// path: that bytes on a wire become an observation nothing can call a fix. A
// fake provider appears only where the companion cannot produce the input --
// the availability values that belong to a transport, and a source with no
// identity at all.
//
// The semantic assertions are deliberately blunt. `PositionValidity::NoFix` is
// asserted at age zero and at an hour, and `Valid` is asserted unreachable, so
// that an ADR-0011 amendment giving `classify()` a case for a coordinate whose
// fix type was never stated cannot land without turning this file red.

#include <array>
#include <cstdio>
#include <cstring>

#include "attadipa/core/location_service.h"
#include "attadipa/link/node_position_provider.h"

namespace {

namespace core = attadipa::core;
namespace link = attadipa::link;

using attadipa::core::Availability;
using attadipa::core::MonotonicTime;
using attadipa::link::MeshCoreCompanion;
using attadipa::link::MeshCoreFrame;

int failures = 0;

#define CHECK(value) check((value), #value, __LINE__)

void check(bool value, const char* expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

constexpr std::uint32_t kHourMs = 3600u * 1000u;

core::MeshPeerId key_of(std::uint8_t seed)
{
    core::MeshPeerId id{};
    for (std::size_t i = 0; i < core::kMeshPublicKeyBytes; ++i) {
        id.public_key[i] = static_cast<std::uint8_t>(seed + i);
    }
    return id;
}

void write_i32(std::uint8_t* out, std::int32_t value)
{
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    out[0] = static_cast<std::uint8_t>(raw & 0xFF);
    out[1] = static_cast<std::uint8_t>((raw >> 8) & 0xFF);
    out[2] = static_cast<std::uint8_t>((raw >> 16) & 0xFF);
    out[3] = static_cast<std::uint8_t>((raw >> 24) & 0xFF);
}

// A RESP_CODE_SELF_INFO carrying an identity and a coordinate in degrees x 10^6,
// which is what the node puts on the wire.
std::array<std::uint8_t, 62> self_info(const core::MeshPeerId& id,
                                       std::int32_t latitude_e6,
                                       std::int32_t longitude_e6)
{
    std::array<std::uint8_t, 62> frame{};
    frame[0] = 5;
    std::memcpy(&frame[4], id.public_key.data(), core::kMeshPublicKeyBytes);
    write_i32(&frame[36], latitude_e6);
    write_i32(&frame[40], longitude_e6);
    std::memcpy(&frame[58], "Node", 4);
    return frame;
}

// Up to and including the coordinate. The contacts sync is not run, so no
// CMD_GET_CUSTOM_VARS goes out and the receiver state stays `Unknown` -- which
// is itself one of the four states a consumer has to be able to show.
void session_with_position(MeshCoreCompanion& client, const core::MeshPeerId& id,
                           std::int32_t latitude_e6, std::int32_t longitude_e6,
                           std::uint64_t begin_ms, std::uint64_t self_info_ms)
{
    client.begin(at(begin_ms));
    client.peer_arriving(at(begin_ms + 1));
    client.connected(at(begin_ms + 2));
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));
    const auto self = self_info(id, latitude_e6, longitude_e6);
    CHECK(client.receive(self.data(), self.size(), at(self_info_ms)));
}

// Everything after the identity, so that RESP_CODE_CUSTOM_VARS has somewhere to
// arrive. Returns nothing: what it is for is the side effect on the client.
void finish_contacts(MeshCoreCompanion& client, std::uint64_t now_ms)
{
    MeshCoreFrame frame{};
    CHECK(client.next_tx(frame));  // CMD_DEVICE_QUERY
    std::uint8_t device[82]{};
    device[0] = 13;
    device[1] = 13;
    CHECK(client.receive(device, sizeof(device), at(now_ms)));
    CHECK(client.next_tx(frame));  // CMD_GET_CONTACTS
    const std::uint8_t start[] = {2, 0, 0, 0, 0};
    CHECK(client.receive(start, sizeof(start), at(now_ms + 1)));
    const std::uint8_t end[] = {4, 0, 0, 0, 0};
    CHECK(client.receive(end, sizeof(end), at(now_ms + 2)));
    CHECK(client.next_tx(frame));  // CMD_SYNC_NEXT_MESSAGE
    CHECK(client.next_tx(frame));
    CHECK(frame.size == 1 && frame.bytes[0] == 40);  // CMD_GET_CUSTOM_VARS
}

void send_custom_vars(MeshCoreCompanion& client, const char* text,
                      std::uint64_t now_ms)
{
    std::uint8_t frame[64]{};
    frame[0] = 21;
    const std::size_t length = std::strlen(text);
    std::memcpy(&frame[1], text, length);
    CHECK(client.receive(frame, length + 1, at(now_ms)));
}

// A COORDINATE BECOMES AN OBSERVATION THAT NOTHING CAN CALL A FIX.
//
// Moscow's centre, on the wire in the node's own units, read back through the
// whole seam. The coordinate survives exactly; everything a fix would carry
// does not exist.
void test_a_node_coordinate_is_never_a_fix()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x91), 55755800, 37617300, 0, 3);
    location.poll();

    const core::LocationState state = location.state(at(4));
    CHECK(state.has_position);
    CHECK(state.position.value.latitude_e7 == 557558000);
    CHECK(state.position.value.longitude_e7 == 376173000);
    CHECK(state.source == core::PositionSource::NodeGnss);
    CHECK(state.fix_type == core::FixType::Unknown);
    CHECK(state.validity == core::PositionValidity::NoFix);
    CHECK(state.availability == Availability::Ready);
    CHECK(state.has_origin);
    CHECK(state.origin == key_of(0x91));

    // Both ages, and the difference between them is the whole point. The one
    // this device measured is a number; the one only the node could have stated
    // has no answer, and `Validity::Unknown` is what says so. A consumer that
    // read `age_at_source_ms` without reading the validity would see a zero and
    // believe the coordinate was sampled the instant it arrived.
    CHECK(state.position.validity == core::Validity::Unknown);
    CHECK(location.age_at_us(at(4)).has_value());
    CHECK(location.age_at_us(at(4))->value == 1);
    CHECK(!location.age_at_source(at(4)).has_value());

    // Nothing a fix would carry was invented.
    const core::GnssObservation& observation = *location.observation();
    CHECK(!observation.satellites_used.has_value());
    CHECK(!observation.satellites_in_view.has_value());
    CHECK(!observation.horizontal_accuracy_mm.has_value());
    CHECK(!observation.hdop_centi.has_value());
    CHECK(!observation.receiver_time.has_value());
    CHECK(!observation.altitude_msl_mm.has_value());
    CHECK(!observation.speed_mm_s.has_value());
    CHECK(observation.jamming == core::ReceiverIndication::Unknown);
    CHECK(observation.spoofing == core::ReceiverIndication::Unknown);
    CHECK(!state.trust.has_value());
}

// THE VERDICT DOES NOT IMPROVE AND DOES NOT DECAY, BECAUSE IT NEVER STARTED.
//
// `classify()` is asked at age zero and at an hour and answers `NoFix` both
// times. It is the same classifier the replay harness uses; nothing here
// second-guesses it, and no path reaches `Valid`, `Degraded` or `Stale`.
void test_the_verdict_is_nofix_at_every_age()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x40), 1000000, 2000000, 0, 3);
    location.poll();

    CHECK(location.state(at(3)).validity == core::PositionValidity::NoFix);
    CHECK(location.state(at(3 + kHourMs)).validity == core::PositionValidity::NoFix);
    CHECK(location.state(at(3 + kHourMs)).has_position);
}

// A LOST FIX IS INVISIBLE, AND THE TEST IS THAT IT STAYS INVISIBLE.
//
// The replay the Definition of Done names: a plausible coordinate, then the
// node loses its fix -- which changes nothing on the wire, because the node's
// own write gate leaves the last value in place -- then the same coordinate for
// an hour. It ends `NoFix`, it was never `Valid`, and neither age was refreshed
// by the repeats.
void test_an_unchanged_coordinate_is_not_evidence_of_a_live_fix()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);
    const core::MeshPeerId id = key_of(0x11);

    session_with_position(client, id, 55755800, 37617300, 0, 100);
    location.poll();
    CHECK(location.age_at_us(at(100))->value == 0);

    bool ever_valid = false;
    for (std::uint32_t minute = 1; minute <= 60; ++minute) {
        const std::uint64_t now = 100 + static_cast<std::uint64_t>(minute) * 60000;
        // The node keeps answering, with the byte-identical coordinate it had
        // before it stopped solving. This is what a lost fix looks like on this
        // wire: nothing.
        client.disconnected(at(now - 3));
        session_with_position(client, id, 55755800, 37617300, now - 2, now - 1);
        location.poll();
        // Not just "never Valid": never anything but `NoFix`. `Degraded` and
        // `Stale` are verdicts about a fix that got worse or got old, and there
        // was never a fix to be either.
        ever_valid = ever_valid ||
                     location.state(at(now)).validity != core::PositionValidity::NoFix;
    }

    const std::uint64_t end = 100 + 60ull * 60000ull;
    CHECK(!ever_valid);
    CHECK(location.state(at(end)).validity == core::PositionValidity::NoFix);
    // The age kept growing across every one of those reads. A refresh on an
    // unchanged value would have reset it to zero sixty times and made an
    // hour-old coordinate look a minute old.
    CHECK(location.age_at_us(at(end))->value == kHourMs);
    CHECK(!location.age_at_source(at(end)).has_value());
}

// A DISCONNECT RETAINS AND AGES. It does not clear, and it does not become
// `NoFix` -- it was `NoFix` already, and clearing would be the node saying "no
// position", which it never did.
void test_a_disconnect_retains_and_ages()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x22), 12345678, -87654321, 0, 10);
    location.poll();
    CHECK(location.state(at(10)).availability == Availability::Ready);

    client.disconnected(at(20));
    location.poll();

    const core::LocationState state = location.state(at(1000));
    CHECK(state.availability == Availability::Unreachable);
    CHECK(state.has_position);
    CHECK(state.position.value.latitude_e7 == 123456780);
    CHECK(state.position.value.longitude_e7 == -876543210);
    CHECK(state.position.age_at_us_ms == 990);
    CHECK(state.validity == core::PositionValidity::NoFix);
}

// A RECEIVER STATE IS A CLAIM ABOUT NOW, AND AN OBSERVATION IS NOT.
//
// The coordinate is retained across a disconnect on purpose: it is a thing the
// node said at a stamped moment and it stays true about that moment. "The
// receiver is running" has no moment attached -- it is a claim about the node
// as it is -- so when the provider can no longer make one, keeping the last is
// asserting a fact from a session that has ended. It printed `recv running`
// beside `avail unreachable` for a node an hour gone, and flipped back to
// `unknown` on the next reconnect with the coordinate never changing.
void test_the_receiver_state_does_not_outlive_its_session()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x33), 12345678, -87654321, 0, 10);
    finish_contacts(client, 11);
    send_custom_vars(client, "gps:1", 14);
    location.poll();
    CHECK(location.state(at(15)).receiver == core::ReceiverPresence::Running);

    client.disconnected(at(20));
    location.poll();

    const core::LocationState gone = location.state(at(kHourMs));
    CHECK(gone.availability == Availability::Unreachable);
    CHECK(gone.receiver == core::ReceiverPresence::Unknown);
    // And the observation beside it is untouched, which is the distinction.
    CHECK(gone.has_position);
    CHECK(gone.position.value.latitude_e7 == 123456780);
    CHECK(gone.validity == core::PositionValidity::NoFix);
}

// A NEW KEY IS A NEW NODE. The retained coordinate is discarded rather than
// re-attributed, because attributing one node's position to another is a fact
// neither of them stated.
void test_a_changed_identity_discards_rather_than_re_attributes()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x01), 10000000, 20000000, 0, 5);
    location.poll();
    CHECK(location.state(at(5)).origin == key_of(0x01));

    client.disconnected(at(10));
    session_with_position(client, key_of(0x77), 30000000, 40000000, 20, 25);
    location.poll();

    const core::LocationState state = location.state(at(25));
    CHECK(state.origin == key_of(0x77));
    CHECK(state.position.value.latitude_e7 == 300000000);
    // The stamp belongs to the new node's read, not to the discarded one's.
    CHECK(state.position.age_at_us_ms == 0);
}

// A REFUSED NODE DOES NOT INHERIT THE ACCEPTED ONE'S COORDINATE.
//
// The refusal happens between the two writes: `receive()` copies the key into
// `status_.node_id` and only then compares it against the pin, breaking before
// the coordinate. So for the whole window until the transport disconnects,
// `node_id()` answers with the stranger and `node_position()` still holds what
// the previous, accepted node said -- and the provider asks them separately,
// because they are separate questions.
//
// Left alone that is worse than a stale reading. The identity is new, so the
// rule above discards the retained observation and immediately re-adopts the
// same coordinate under the stranger's key, which launders one node's position
// into another node's name and reports `Ready` for a session the companion has
// already stopped listening to. It does not self-heal either: a second
// RESP_CODE_SELF_INFO does not re-run the identity settle, so it stands until
// BLE drops for some other reason.
void test_a_refused_node_does_not_inherit_the_accepted_coordinate()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    client.pin(key_of(0x01));
    session_with_position(client, key_of(0x01), 10000000, 20000000, 0, 5);
    location.poll();
    CHECK(location.state(at(5)).origin == key_of(0x01));
    CHECK(location.state(at(5)).position.value.latitude_e7 == 100000000);

    // The same session, a second RESP_CODE_SELF_INFO, a different key.
    const auto stranger = self_info(key_of(0x77), 30000000, 40000000);
    CHECK(client.receive(stranger.data(), stranger.size(), at(8)));
    CHECK(client.wrong_node());

    location.poll();
    const core::LocationState state = location.state(at(9));

    // The accepted node keeps its coordinate and its name, and the age goes on
    // measuring from when *it* arrived.
    CHECK(state.origin == key_of(0x01));
    CHECK(state.has_position);
    CHECK(state.position.value.latitude_e7 == 100000000);
    CHECK(state.position.age_at_us_ms == 4);
    // And nothing claims the refused node is a working source.
    CHECK(state.availability != Availability::Ready);
    CHECK(state.receiver == core::ReceiverPresence::Unknown);
}

// FORGETTING A NODE WITHDRAWS WHAT IT SAID; A DISCONNECT DOES NOT.
//
// The two are one line apart in the owner and opposite in meaning, which is why
// both are pinned here. `test_a_disconnect_retains_and_ages` fixes the first:
// a node that went away did not take back its coordinate. `forget()` is the
// second: after #411 the watch is unpaired, will not reconnect and has deleted
// the bond, so continuing to report that node's position and key prefix states
// a source the watch has repudiated -- for as long as no other node states one,
// which may be forever.
void test_forgetting_a_node_withdraws_its_coordinate()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0x01), 10000000, 20000000, 0, 5);
    location.poll();
    CHECK(location.state(at(5)).has_position);
    CHECK(location.state(at(5)).has_origin);

    // A disconnect alone retains -- the case the line above this one exists for.
    client.disconnected(at(10));
    location.poll();
    CHECK(location.state(at(20)).has_position);
    CHECK(location.state(at(20)).origin == key_of(0x01));

    location.forget();
    const core::LocationState state = location.state(at(30));
    CHECK(!state.has_position);
    CHECK(!state.has_origin);
    CHECK(!location.observation().has_value());
    CHECK(!location.age_at_us(at(30)).has_value());
    CHECK(state.receiver == core::ReceiverPresence::Unknown);

    // And it is a withdrawal, not a mute: a node that states one afterwards is
    // adopted normally.
    session_with_position(client, key_of(0x55), 50000000, 60000000, 40, 45);
    location.poll();
    CHECK(location.state(at(45)).has_position);
    CHECK(location.state(at(45)).origin == key_of(0x55));
    CHECK(location.state(at(45)).position.value.latitude_e7 == 500000000);
}

// THE THREE STATES OF THE `gps` KEY REACH THE CONSUMER AND CHANGE NOTHING ELSE.
//
// The receiver state is a fact about the coordinate's provenance, never a
// verdict on it: all three leave the validity at `NoFix` and the availability
// at `Ready`. In particular a node whose receiver is switched off is not `Off`
// -- that is a remedy this watch cannot perform.
void test_the_receiver_state_is_carried_and_changes_no_verdict()
{
    struct Case {
        const char* vars;
        core::ReceiverPresence expected;
    };
    const Case cases[] = {
        {"", core::ReceiverPresence::NotDetected},
        {"batt:4100", core::ReceiverPresence::NotDetected},
        {"gps:0", core::ReceiverPresence::PoweredOff},
        {"gps:1", core::ReceiverPresence::Running},
        {"batt:4100,gps:1", core::ReceiverPresence::Running},
        {"gps:", core::ReceiverPresence::Unknown},
        {"gps:x", core::ReceiverPresence::Unknown},
    };

    for (const Case& one : cases) {
        MeshCoreCompanion client;
        link::NodePositionProvider provider(client);
        core::LocationService location(provider);

        session_with_position(client, key_of(0x55), 1000000, 1000000, 0, 3);
        finish_contacts(client, 4);
        send_custom_vars(client, one.vars, 10);
        location.poll();

        const core::LocationState state = location.state(at(11));
        CHECK(state.receiver == one.expected);
        CHECK(state.validity == core::PositionValidity::NoFix);
        CHECK(state.availability == Availability::Ready);
        CHECK(state.has_position);
    }

    // And a node too old to define opcode 40 answers RESP_CODE_ERR. That is a
    // normal outcome, not an error to the user: the state stays `Unknown`, the
    // coordinate is untouched and the session carries on.
    MeshCoreCompanion old_node;
    link::NodePositionProvider provider(old_node);
    core::LocationService location(provider);
    session_with_position(old_node, key_of(0x55), 1000000, 1000000, 0, 3);
    finish_contacts(old_node, 4);
    const std::uint8_t error[] = {1, 1};
    CHECK(old_node.receive(error, sizeof(error), at(10)));
    location.poll();
    CHECK(location.state(at(11)).receiver == core::ReceiverPresence::Unknown);
    CHECK(location.state(at(11)).availability == Availability::Ready);
    CHECK(location.state(at(11)).has_position);
    CHECK(old_node.malformed_frames() == 0);
}

// A TYPED COORDINATE AND A SOLVED ONE ARE THE SAME BYTES.
//
// `CMD_SET_ADVERT_LATLON` writes the same two fields a receiver does, so an
// owner who typed their node's position into a phone produces a frame this
// watch cannot tell from a fix. The test asserts that indistinguishability
// rather than papering over it: the two observations are identical in every
// field, and the only thing that differs is the receiver state beside them.
void test_a_typed_coordinate_is_indistinguishable_from_a_solved_one()
{
    core::GnssObservation observations[2];
    core::ReceiverPresence receivers[2];

    const char* vars[2] = {"gps:1", ""};
    for (int which = 0; which < 2; ++which) {
        MeshCoreCompanion client;
        link::NodePositionProvider provider(client);
        core::LocationService location(provider);

        session_with_position(client, key_of(0x33), 51507400, -127800, 0, 3);
        finish_contacts(client, 4);
        send_custom_vars(client, vars[which], 10);
        location.poll();
        observations[which] = *location.observation();
        receivers[which] = location.state(at(11)).receiver;
    }

    CHECK(observations[0].position->latitude_e7 == observations[1].position->latitude_e7);
    CHECK(observations[0].position->longitude_e7 == observations[1].position->longitude_e7);
    CHECK(observations[0].fix_type == observations[1].fix_type);
    CHECK(observations[0].source == observations[1].source);
    CHECK(observations[0].observed_at == observations[1].observed_at);
    CHECK(observations[0].satellites_used == observations[1].satellites_used);
    CHECK(observations[0].horizontal_accuracy_mm == observations[1].horizontal_accuracy_mm);
    // The one difference, and it is beside the observation rather than in it.
    CHECK(receivers[0] == core::ReceiverPresence::Running);
    CHECK(receivers[1] == core::ReceiverPresence::NotDetected);
}

// THE VALUES AT THE EDGE OF THE GLOBE, AND THE ONE BEYOND IT.
//
// (0, 0) is legal, plausible and almost certainly an unset preference, and is
// accepted like any other coordinate. The poles and the antimeridian are
// accepted at the boundary. A value outside is dropped -- the node's own
// `CMD_SET_ADVERT_LATLON` check should make it unreachable, and a peer's output
// is not trusted to have run its own checks.
void test_the_boundary_values_and_the_one_past_it()
{
    struct Case {
        std::int32_t latitude_e6;
        std::int32_t longitude_e6;
        bool accepted;
    };
    const Case cases[] = {
        {0, 0, true},
        {90000000, 180000000, true},
        {-90000000, -180000000, true},
        {90000001, 0, false},
        {0, 180000001, false},
        // Multiplied by ten this overflows a signed 32-bit value, which is the
        // input the widening in the decoder exists for.
        {2000000000, 0, false},
        {-2147483647 - 1, 0, false},
    };

    for (const Case& one : cases) {
        MeshCoreCompanion client;
        link::NodePositionProvider provider(client);
        core::LocationService location(provider);

        session_with_position(client, key_of(0x66), one.latitude_e6,
                              one.longitude_e6, 0, 3);
        location.poll();
        CHECK(location.state(at(4)).has_position == one.accepted);
        // Rejecting a field is not rejecting the frame: the identity in it is
        // what the rest of the session runs on and it survives either way.
        CHECK(client.status().has_node_id);
        CHECK(client.malformed_frames() == 0);
    }
}

// A provider with no identity to offer, which the companion cannot be. It
// stands in for the local receiver this tree does not have yet, and pins the
// one behaviour that would otherwise be decided by accident: with no identity
// there is nothing to compare, so the discard rule never fires and a source
// that cannot be swapped underneath us keeps its observation.
class AnonymousProvider final : public core::PositionProvider {
public:
    Availability availability() const override { return availability_; }

    bool sample(core::PositionSample& out) const override
    {
        if (!has_sample_) return false;
        out = sample_;
        return true;
    }

    void set(Availability availability) { availability_ = availability; }

    void offer(std::int32_t latitude_e7, std::int32_t longitude_e7,
               MonotonicTime observed_at)
    {
        sample_ = core::PositionSample{};
        sample_.observation.position = core::Position{latitude_e7, longitude_e7};
        sample_.observation.observed_at = observed_at;
        sample_.observation.source = core::PositionSource::NodeGnss;
        has_sample_ = true;
    }

    void withdraw() { has_sample_ = false; }

private:
    Availability availability_ = Availability::Unprovisioned;
    core::PositionSample sample_{};
    bool has_sample_ = false;
};

// EVERY AVAILABILITY A NODE PROVIDER CAN PRODUCE RENDERS AS ITSELF, and the
// position underneath is unaffected by all of them. Seven states are seven
// sentences (ADR-0004 §3); a service that collapsed any pair here would be
// telling a user to do the wrong thing.
void test_availability_travels_without_touching_the_position()
{
    AnonymousProvider provider;
    core::LocationService location(provider);

    provider.set(Availability::Unprovisioned);
    location.poll();
    CHECK(location.state(at(0)).availability == Availability::Unprovisioned);
    CHECK(!location.state(at(0)).has_position);

    provider.offer(557558000, 376173000, at(10));
    provider.set(Availability::Ready);
    location.poll();
    CHECK(location.state(at(10)).has_position);

    const Availability states[] = {Availability::Unreachable,
                                   Availability::Incompatible,
                                   Availability::Failed};
    for (Availability one : states) {
        provider.set(one);
        provider.withdraw();
        location.poll();
        const core::LocationState state = location.state(at(1010));
        CHECK(state.availability == one);
        CHECK(state.has_position);
        CHECK(state.position.value.latitude_e7 == 557558000);
        CHECK(state.position.age_at_us_ms == 1000);
        CHECK(state.validity == core::PositionValidity::NoFix);
    }

    // No identity, so nothing is ever discarded for changing.
    provider.offer(557558000, 376173000, at(2000));
    provider.set(Availability::Ready);
    location.poll();
    CHECK(location.state(at(2000)).has_position);
    CHECK(!location.state(at(2000)).has_origin);
    // ...and the repeat did not refresh the age either.
    CHECK(location.state(at(2000)).position.age_at_us_ms == 1990);
}

// THE ENGINEERING LINE MAKES THE UNCERTAINTY THE SUBJECT.
//
// The first consumer of a position in this repository is not a map. It shows
// the coordinate, both ages, the validity, the receiver state and the node's
// key -- and writes `UNKNOWN` in full wherever a number would imply a
// measurement nobody made.
void test_the_engineering_line_says_unknown_where_nothing_is_known()
{
    MeshCoreCompanion client;
    link::NodePositionProvider provider(client);
    core::LocationService location(provider);

    session_with_position(client, key_of(0xAB), 55755800, 37617300, 0, 3);
    finish_contacts(client, 4);
    send_custom_vars(client, "gps:0", 10);
    location.poll();

    char line[256]{};
    const std::size_t written =
        core::format_location_line(location.state(at(1003)), line, sizeof(line));
    CHECK(written > 0 && written < sizeof(line));
    CHECK(std::strstr(line, "pos 55.7558000,37.6173000") != nullptr);
    CHECK(std::strstr(line, "age_us 1000ms") != nullptr);
    CHECK(std::strstr(line, "age_src UNKNOWN") != nullptr);
    CHECK(std::strstr(line, "validity NoFix") != nullptr);
    CHECK(std::strstr(line, "recv off") != nullptr);
    CHECK(std::strstr(line, "node abacadae") != nullptr);

    // With nothing read at all, every field that would be a number says so.
    core::LocationState empty;
    char blank[256]{};
    CHECK(core::format_location_line(empty, blank, sizeof(blank)) > 0);
    CHECK(std::strstr(blank, "pos UNKNOWN,UNKNOWN") != nullptr);
    CHECK(std::strstr(blank, "age_us UNKNOWN") != nullptr);
    CHECK(std::strstr(blank, "node UNKNOWN") != nullptr);

    // A negative coordinate is written with its sign and its full precision,
    // and a buffer too small truncates rather than overruns.
    core::LocationState south;
    south.has_position = true;
    south.position.value = core::Position{-337520000, -1804000};
    char southern[256]{};
    CHECK(core::format_location_line(south, southern, sizeof(southern)) > 0);
    CHECK(std::strstr(southern, "pos -33.7520000,-0.1804000") != nullptr);
    char cramped[8]{};
    CHECK(core::format_location_line(south, cramped, sizeof(cramped)) >= sizeof(cramped));
    CHECK(cramped[sizeof(cramped) - 1] == '\0');
}

}  // namespace

int main()
{
    test_a_node_coordinate_is_never_a_fix();
    test_the_verdict_is_nofix_at_every_age();
    test_an_unchanged_coordinate_is_not_evidence_of_a_live_fix();
    test_a_disconnect_retains_and_ages();
    test_the_receiver_state_does_not_outlive_its_session();
    test_a_changed_identity_discards_rather_than_re_attributes();
    test_a_refused_node_does_not_inherit_the_accepted_coordinate();
    test_forgetting_a_node_withdraws_its_coordinate();
    test_the_receiver_state_is_carried_and_changes_no_verdict();
    test_a_typed_coordinate_is_indistinguishable_from_a_solved_one();
    test_the_boundary_values_and_the_one_past_it();
    test_availability_travels_without_touching_the_position();
    test_the_engineering_line_says_unknown_where_nothing_is_known();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("location service: all checks passed\n");
    return 0;
}
