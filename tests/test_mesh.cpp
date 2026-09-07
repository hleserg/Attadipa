// What the mesh readout says, with no panel anywhere near it.
//
// `apps::format_mesh()` is where the screen stopped printing enumerator names
// and started leaving fields empty rather than filling them with zeroes, so
// this is where both of those are held. It links `attadipa_apps` and nothing
// else: a test that needed LVGL to ask what the words are would be testing the
// drawing, and the drawing is not what decides them.

#include <cstdio>
#include <cstring>

#include "attadipa/apps/mesh.h"

using namespace attadipa;

namespace {

int failures = 0;

void check(bool condition, const char *what, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
    ++failures;
  }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

core::MeshPeerId key(std::uint32_t bytes) {
  core::MeshPeerId id{};
  id.public_key[0] = static_cast<std::uint8_t>(bytes >> 24U);
  id.public_key[1] = static_cast<std::uint8_t>(bytes >> 16U);
  id.public_key[2] = static_cast<std::uint8_t>(bytes >> 8U);
  id.public_key[3] = static_cast<std::uint8_t>(bytes);
  return id;
}

core::MeshStatus linked_status() {
  core::MeshStatus status;
  status.availability = core::Availability::Ready;
  status.transport = core::TransportPhase::Ready;
  status.node_id = key(0x4C9A2F7BU);
  status.has_node_id = true;
  std::snprintf(status.node_name.data(), status.node_name.size(), "Ridge");
  std::snprintf(status.last_message.data(), status.last_message.size(), "here");
  std::snprintf(status.last_sender.data(), status.last_sender.size(), "Ridge");
  status.delivery = core::MeshDelivery::Confirmed;
  // 29 quarters is 7.25 dB. Deliberately not a multiple of four: a value that
  // divided evenly would pass whether or not the remainder is printed at all,
  // and the quarter is the whole reason this is not an integer field.
  status.snr_quarter_db = 29;
  status.has_snr = true;
  status.peers_reported = 3;
  status.mtu = 244;
  return status;
}

// Every phase produces its own answer, and none of them produces an enumerator
// name. The mapping is the thing that replaced `core::to_string(transport)`, so
// a phase silently folding into a neighbour is the regression to catch.
void every_phase_has_its_own_word() {
  const struct {
    core::TransportPhase phase;
    apps::MeshLink link;
  } kCases[] = {
      {core::TransportPhase::Absent, apps::MeshLink::NoRadio},
      {core::TransportPhase::Attached, apps::MeshLink::Silent},
      {core::TransportPhase::Connecting, apps::MeshLink::Reaching},
      {core::TransportPhase::Ready, apps::MeshLink::Linked},
      {core::TransportPhase::Suspended, apps::MeshLink::Resting},
      {core::TransportPhase::Faulted, apps::MeshLink::Broken},
  };
  for (const auto &c : kCases) {
    core::MeshStatus status;
    status.availability = core::Availability::Unreachable;
    status.transport = c.phase;
    const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
    CHECK(text.link == c.link);
    CHECK(text.state[0] != '\0');
    // The words a wearer must never see, because they are C++ identifiers.
    CHECK(std::strcmp(text.state, "Attached") != 0);
    CHECK(std::strcmp(text.state, "Connecting") != 0);
    CHECK(std::strcmp(text.state, "Suspended") != 0);
    CHECK(std::strcmp(text.state, "Faulted") != 0);
  }
}

// NOTHING IS DRAWN FOR WHAT IS NOT THERE.
//
// This is the honest-state rule as an assertion rather than a comment: a watch
// with no node named has no peers, no MTU and no SNR, and the readout must not
// hand the face a zero for any of them. The screen this replaced printed
// `Peers: 0` and `MTU: 0` in exactly this state.
void an_unnamed_node_reports_no_measurements() {
  core::MeshStatus status;
  status.availability = core::Availability::Unprovisioned;
  status.transport = core::TransportPhase::Absent;
  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);

  CHECK(text.link == apps::MeshLink::NoNode);
  CHECK(!text.has_signal);
  CHECK(!text.has_message);
  CHECK(!text.has_mtu);
  CHECK(!text.has_snr);
  CHECK(text.peers[0] == '\0');
  CHECK(text.mtu[0] == '\0');
  CHECK(text.snr[0] == '\0');
  CHECK(text.message[0] == '\0');
  CHECK(text.message_heading[0] == '\0');
  CHECK(text.rungs_lit == 0);
  // There is a way out of this one, and it is the only thing on the screen the
  // wearer can act on.
  CHECK(text.way_out[0] != '\0');
}

// A watch that cannot carry a mesh is offered no way out, because there is
// none. An instruction that does nothing is worse than no instruction.
void an_impossible_state_offers_no_instruction() {
  core::MeshStatus status;
  status.availability = core::Availability::Unsupported;
  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
  CHECK(text.link == apps::MeshLink::NoRadio);
  CHECK(text.way_out[0] == '\0');
}

// A refusal outranks the phase. `Ready` is used deliberately: it is the phase
// the refusal has to beat, and a check that ran after the transport switch
// would answer `Linked` here.
void a_refusal_outranks_the_phase() {
  core::MeshStatus status = linked_status();
  status.pinned_id = key(0x4C9A2F7BU);
  status.has_pinned = true;
  status.refused_id = key(0x9E14C003U);
  status.has_refused = true;

  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
  CHECK(text.link == apps::MeshLink::TurnedAway);
  CHECK(std::strstr(text.pinned, "4c9a2f7b") != nullptr);
  CHECK(std::strstr(text.answered, "9e14c003") != nullptr);
  // The two keys must not read as the same node: that is the entire reason a
  // prefix is on the screen instead of a name.
  CHECK(std::strcmp(text.pinned, text.answered) != 0);
}

// `Resting` fills the same six values as `Linked` and must not look the same
// doing it. There is no age in `core::MeshStatus`, so the distinction cannot be
// "how long ago" -- it is that one block is arriving and the other is history.
void a_quiesced_link_says_its_data_is_history() {
  const apps::MeshText live = apps::format_mesh(linked_status(), l10n::Locale::En);
  core::MeshStatus quiet = linked_status();
  quiet.transport = core::TransportPhase::Suspended;
  const apps::MeshText held = apps::format_mesh(quiet, l10n::Locale::En);

  CHECK(live.live);
  CHECK(!held.live);
  CHECK(live.has_message && held.has_message);
  // Same values, different claim about them.
  CHECK(std::strcmp(live.message, held.message) == 0);
  CHECK(std::strcmp(live.snr, held.snr) == 0);
  CHECK(std::strcmp(live.message_heading, held.message_heading) != 0);
}

// An MTU nobody negotiated is absent, not zero.
void an_unnegotiated_mtu_is_absent() {
  core::MeshStatus status = linked_status();
  status.mtu = 0;
  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
  CHECK(text.has_signal);        // the link is up; peers and SNR are real
  CHECK(!text.has_mtu);
  CHECK(text.mtu[0] == '\0');
  CHECK(text.peers[0] != '\0');  // and this one is not suppressed with it
}

void the_signal_to_noise_ratio_keeps_its_quarters() {
  core::MeshStatus status = linked_status();
  const apps::MeshText up = apps::format_mesh(status, l10n::Locale::En);
  CHECK(std::strcmp(up.snr, "7.25") == 0);

  status.snr_quarter_db = -29;
  const apps::MeshText down = apps::format_mesh(status, l10n::Locale::En);
  CHECK(std::strcmp(down.snr, "-7.25") == 0);

  status.has_snr = false;
  const apps::MeshText none = apps::format_mesh(status, l10n::Locale::En);
  CHECK(none.snr[0] == '\0');
  CHECK(none.snr_label[0] == '\0');
  CHECK(none.peers[0] != '\0');  // peers survive an unmeasured SNR
}

// Four bytes, eight characters, every nibble in the right place. The bytes are
// chosen so that no two are equal and none is a palindrome under a nibble swap:
// `0x4C9A2F7B` distinguishes a correct conversion from one that reversed the
// nibbles, the bytes, or both.
void a_key_prefix_is_eight_hex_characters() {
  char hex[9] = {};
  apps::mesh_key_prefix(key(0x4C9A2F7BU), hex);
  CHECK(std::strcmp(hex, "4c9a2f7b") == 0);
}

// The channel is not a two-state light. `Reaching` lights strictly some of it,
// which is what "on its way" looks like without a word.
void reaching_lights_part_of_the_channel() {
  core::MeshStatus status;
  status.availability = core::Availability::Unreachable;
  status.transport = core::TransportPhase::Connecting;
  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
  CHECK(text.rungs_lit > 0);
  CHECK(text.rungs_lit < apps::kChannelRungs);
}

// Both locales answer, and they answer differently. A field that came back
// identical in both would be an untranslated literal, which is the defect the
// state words were.
void both_locales_are_answered() {
  core::MeshStatus status;
  status.availability = core::Availability::Unprovisioned;
  const apps::MeshText en = apps::format_mesh(status, l10n::Locale::En);
  const apps::MeshText ru = apps::format_mesh(status, l10n::Locale::Ru);
  CHECK(en.state[0] != '\0' && ru.state[0] != '\0');
  CHECK(std::strcmp(en.state, ru.state) != 0);
  CHECK(std::strcmp(en.note, ru.note) != 0);
  CHECK(std::strcmp(en.way_out, ru.way_out) != 0);
}

// EVERY FIELD, EVERY STATE, NEVER CUT OFF.
//
// `put()` is `snprintf`, which truncates in silence, at a byte, and returns a
// length nobody was reading -- so a Russian word one byte over its buffer came
// back a character short and looked like a translation, and a cut landing
// inside a two-byte code point put half a character on the panel. Two fields
// were doing it and neither was in a fixture: the default delivery word, which
// is what a watch shows before it has sent anything, and any message longer
// than the four letters `linked_status()` puts there.
//
// So this asserts no expected string at all. A truncated `snprintf` always
// fills its buffer exactly, so `strlen == sizeof - 1` is the signature of the
// defect whatever the catalogue happens to say today, and the sweep walks
// every state the formatter can reach rather than the handful somebody thought
// to write down. Sizing a buffer by looking at the strings is the mistake this
// replaces, not the fix for it.
#define MESH_TEXT_FIELDS(X)                                                    \
  X(title) X(state) X(note) X(way_out) X(node_key) X(node_name) X(pinned)      \
  X(answered) X(message_heading) X(message) X(sender) X(delivery) X(snr)       \
  X(snr_label) X(peers) X(peers_label) X(mtu) X(mtu_label)

void no_field_is_ever_cut_short() {
  const l10n::Locale locales[] = {l10n::Locale::En, l10n::Locale::Ru};
  int swept = 0;
  for (int a = 0; a <= static_cast<int>(core::Availability::Ready); ++a) {
    for (int t = 0; t <= static_cast<int>(core::TransportPhase::Faulted); ++t) {
      for (int d = 0; d <= static_cast<int>(core::MeshDelivery::Failed); ++d) {
        for (int refused = 0; refused < 2; ++refused) {
          for (l10n::Locale locale : locales) {
            core::MeshStatus status = linked_status();
            status.availability = static_cast<core::Availability>(a);
            status.transport = static_cast<core::TransportPhase>(t);
            status.delivery = static_cast<core::MeshDelivery>(d);
            status.has_pinned = refused != 0;
            status.has_refused = refused != 0;
            status.pinned_id = key(0x11223344U);
            status.refused_id = key(0x55667788U);
            // The longest name and sender either side can carry, because a
            // buffer that fits the fixture's "Ridge" proves nothing about the
            // 32 bytes `core::MeshStatus` is willing to hold.
            for (std::size_t i = 0; i < core::kMeshPeerNameBytes; ++i) {
              status.node_name[i] = 'N';
              status.last_sender[i] = 'S';
            }
            const apps::MeshText text = apps::format_mesh(status, locale);
            ++swept;
#define CHECK_NOT_TRUNCATED(field)                                             \
  if (std::strlen(text.field) == sizeof(text.field) - 1) {                     \
    std::fprintf(stderr,                                                       \
                 "FAIL line %d: %s filled its %zu-byte buffer: \"%s\"\n",      \
                 __LINE__, #field, sizeof(text.field), text.field);            \
    ++failures;                                                                \
  }
            MESH_TEXT_FIELDS(CHECK_NOT_TRUNCATED)
#undef CHECK_NOT_TRUNCATED
          }
        }
      }
    }
  }
  CHECK(swept == 7 * 6 * 5 * 2 * 2);
}

// And the message itself arrives whole. The sweep above cannot make this claim:
// a message that exactly fills its buffer is indistinguishable from one that
// was cut to fit, which is why the longest one is asserted against what went in
// rather than against its length.
void the_longest_message_arrives_whole() {
  core::MeshStatus status = linked_status();
  for (std::size_t i = 0; i < core::kMeshTextBytes; ++i) {
    status.last_message[i] = static_cast<char>('a' + (i % 26));
  }
  const apps::MeshText text = apps::format_mesh(status, l10n::Locale::En);
  CHECK(std::strlen(text.message) == core::kMeshTextBytes);
  CHECK(std::strcmp(text.message, status.last_message.data()) == 0);
}

}  // namespace

int main() {
  every_phase_has_its_own_word();
  an_unnamed_node_reports_no_measurements();
  an_impossible_state_offers_no_instruction();
  a_refusal_outranks_the_phase();
  a_quiesced_link_says_its_data_is_history();
  an_unnegotiated_mtu_is_absent();
  the_signal_to_noise_ratio_keeps_its_quarters();
  a_key_prefix_is_eight_hex_characters();
  reaching_lights_part_of_the_channel();
  both_locales_are_answered();
  no_field_is_ever_cut_short();
  the_longest_message_arrives_whole();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("mesh readout: all checks passed\n");
  return 0;
}
