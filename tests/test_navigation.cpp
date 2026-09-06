#include <cstdio>
#include <cstring>

#include "attadipa/apps/navigation.h"

// Host tests for the navigation readout.
//
// The subject is not the arithmetic — `tests/test_position.cpp` owns distance
// and bearing. It is the rule the owner's brief states as
// `NoFix != stale != current != unknown`: which of eight sentences the screen
// says, and, in every one of them, whether a number appears at all. A readout
// that prints `0 m` or `000°` where it means "nobody knows" is the defect this
// file exists to catch, and it is a defect that no build and no crash reveals.

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);          \
      ++failures;                                                              \
    }                                                                          \
  } while (false)

using namespace attadipa;

constexpr std::int32_t kDeg = 10000000;

// Half a degree north, one degree east — the Gulf of Guinea, the same
// deliberately-nowhere place `tests/test_position.cpp` and the replay traces
// use. No real location belonging to anybody appears in this repository.
constexpr core::Position kHere{5000000, 10000000};

core::LocationState own_fix(core::Position position,
                            core::PositionValidity validity = core::PositionValidity::Valid) {
  core::LocationState state;
  state.availability = core::Availability::Ready;
  state.has_position = true;
  state.position.value = position;
  state.validity = validity;
  state.fix_type = core::FixType::ThreeD;
  state.source = core::PositionSource::LocalGnss;
  state.receiver = core::ReceiverPresence::Running;
  return state;
}

// What the MeshCore node link actually produces: a coordinate, an arrival age,
// no fix type and therefore `NoFix` forever.
core::LocationState node_coordinate(core::Position position, std::uint32_t age_ms) {
  core::LocationState state;
  state.availability = core::Availability::Ready;
  state.has_position = true;
  state.position.value = position;
  state.position.age_at_us_ms = age_ms;
  state.validity = core::PositionValidity::NoFix;
  state.fix_type = core::FixType::Unknown;
  state.source = core::PositionSource::NodeGnss;
  return state;
}

core::Heading watch_heading(std::uint16_t centideg,
                            core::HeadingValidity validity = core::HeadingValidity::Valid,
                            std::uint8_t confidence = 90) {
  core::Heading heading;
  heading.centideg = centideg;
  heading.source = core::HeadingSource::Magnetometer;
  heading.frame = core::ReferenceFrame::WatchBody;
  heading.confidence = confidence;
  heading.validity = validity;
  return heading;
}

bool is(const char *actual, const char *expected) {
  return std::strcmp(actual, expected) == 0;
}

// Every status that is not a rendered pair of numbers must show the em dash in
// both fields. Called from each of those tests rather than once at the end, so
// a failure names the state that produced it.
void check_no_numbers(const apps::NavText &text) {
  CHECK(!text.has_distance);
  CHECK(!text.has_bearing);
  CHECK(is(text.distance, "—"));
  CHECK(is(text.bearing, "—"));
  CHECK(is(text.cardinal, ""));
}

void test_a_watch_that_knows_nothing_says_so() {
  apps::NavState state;
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::WaitingForGps);
  CHECK(is(text.status, "Waiting for GPS"));
  check_no_numbers(text);
}

void test_a_receiver_that_answered_no_is_not_a_receiver_still_starting() {
  // The difference between these two is the whole reason both exist: one is
  // "wait", the other is "go outside".
  apps::NavState waiting;
  waiting.own.availability = core::Availability::Ready;
  waiting.own.receiver = core::ReceiverPresence::Running;
  CHECK(apps::format_navigation(waiting).status_code == apps::NavStatus::WaitingForGps);

  apps::NavState refused = waiting;
  refused.own.fix_type = core::FixType::NoFix;
  const apps::NavText text = apps::format_navigation(refused);
  CHECK(text.status_code == apps::NavStatus::NoFix);
  CHECK(is(text.status, "No fix"));
  check_no_numbers(text);
}

void test_a_local_coordinate_with_no_fix_behind_it_is_not_a_position() {
  // A receiver on this body reports its own fix state. `NoFix` there is the
  // receiver saying so, and the coordinate beside it is stale memory, not an
  // answer — so no number is drawn from it.
  apps::NavState state;
  state.own = own_fix(kHere, core::PositionValidity::NoFix);
  state.own.fix_type = core::FixType::NoFix;
  state.target = node_coordinate({5100000, 10000000}, 1000);
  check_no_numbers(apps::format_navigation(state));
}

void test_the_node_is_the_one_source_allowed_to_state_no_fix_and_still_count() {
  // The asymmetry with the test above, and the product depends on it: a
  // MeshCore node states no fix type at all, so `classify()` answers `NoFix`
  // for every coordinate it will ever send. Refusing on that would refuse
  // every node coordinate, which is the entire feature.
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 3000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::Ready);
  CHECK(text.has_distance);
  CHECK(text.has_bearing);
  // Due north, about 1.1 km.
  CHECK(is(text.bearing, "000°"));
  CHECK(is(text.cardinal, "N"));
  CHECK(is(text.distance, "1.1 km"));
}

void test_ready_still_says_what_is_not_known() {
  // The caveat does not go away on a good day. A node coordinate arrives with
  // no fix type, no observation time and no satellite count, and `Ready`
  // describes the readout, never the node's fix.
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 3000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::Ready);
  CHECK(std::strstr(text.caveat, "unverified") != nullptr);
  CHECK(std::strstr(text.caveat, "3 s ago") != nullptr);
}

void test_an_old_node_coordinate_leads_with_its_age() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 300000);  // five minutes
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::NodePositionStale);
  CHECK(is(text.status, "Node position stale"));
  // And the numbers stay: the last thing it said is still the best answer
  // anyone has, and the line above says how old it is.
  CHECK(text.has_distance);
  CHECK(std::strstr(text.caveat, "5 min ago") != nullptr);
}

void test_the_threshold_belongs_to_the_caller() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 30000);
  CHECK(apps::format_navigation(state).status_code == apps::NavStatus::Ready);
  state.target_stale_after = core::Millis{10000};
  CHECK(apps::format_navigation(state).status_code == apps::NavStatus::NodePositionStale);
}

void test_a_dropped_link_is_reported_as_a_link_and_not_as_a_coordinate() {
  // `LocationService` retains what a node said across a disconnect on purpose.
  // The status names the link; the numbers go on rendering from the retained
  // coordinate, which is exactly what "last known" means.
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 3000);
  state.target.availability = core::Availability::Unreachable;
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::NodeUnavailable);
  CHECK(is(text.status, "Node unavailable"));
  CHECK(text.has_distance);
}

// The own half of the test above, and the one that was missing: a receiver
// that is bound and has stopped answering said nothing the wearer could see.
// It read `Ready` with a full distance while its last coordinate stayed inside
// the freshness window, then `OwnPositionStale` — a claim about the age of a
// coordinate when the story is that nothing is arriving at all.
void test_a_receiver_that_stopped_answering_is_reported_as_a_receiver() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.own.availability = core::Availability::Unreachable;
  state.target = node_coordinate({5100000, 10000000}, 3000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::OwnReceiverSilent);
  CHECK(is(text.status, "Receiver silent"));
  // The retained coordinate goes on measuring, for the same reason the node's
  // does across a dropped link: it was real, and the line above says it is no
  // longer being refreshed.
  CHECK(text.has_distance);

  // And it outranks what the coordinate's own age would have said.
  state.own.validity = core::PositionValidity::Stale;
  CHECK(apps::format_navigation(state).status_code ==
        apps::NavStatus::OwnReceiverSilent);

  // Never having answered is the same sentence. This is the one that used to
  // read "Waiting for GPS" — indistinguishable from a healthy receiver still
  // acquiring, which is the state a wearer is meant to wait out rather than go
  // and check the wiring for.
  apps::NavState never;
  never.own.availability = core::Availability::Unreachable;
  never.own.receiver = core::ReceiverPresence::Running;
  const apps::NavText cold = apps::format_navigation(never);
  CHECK(cold.status_code == apps::NavStatus::OwnReceiverSilent);
  check_no_numbers(cold);
}

void test_a_node_that_never_said_where_it_is() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target.availability = core::Availability::Ready;
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::NodePositionUnknown);
  check_no_numbers(text);
}

void test_a_coordinate_off_the_globe_is_refused_rather_than_saturated() {
  // Untrusted input arrives here: a node coordinate is bytes off a radio.
  // `great_circle_mm()` answers `kDistanceSaturated` for out-of-range as well
  // as for far away, so without this guard a hostile coordinate renders as a
  // confident "> 1000 km".
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({core::kLatitudeMaxE7 + 1, 0}, 1000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::NodePositionUnknown);
  check_no_numbers(text);

  // And the same in the other direction.
  apps::NavState mirrored;
  mirrored.own = own_fix({0, -core::kLongitudeMaxE7 - 1});
  mirrored.target = node_coordinate(kHere, 1000);
  check_no_numbers(apps::format_navigation(mirrored));
}

void test_a_stale_own_fix_is_neither_a_missing_one_nor_a_current_one() {
  apps::NavState state;
  state.own = own_fix(kHere, core::PositionValidity::Stale);
  state.target = node_coordinate({5100000, 10000000}, 3000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::OwnPositionStale);
  CHECK(is(text.status, "Own position stale"));
  CHECK(text.has_distance);
}

void test_standing_on_it_is_a_measured_zero_and_not_a_direction() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate(kHere, 1000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.has_distance);
  CHECK(is(text.distance, "0 m"));
  // There is no direction to where you already are, and the needle must not be
  // drawn. This is the one place `0 m` is honest and `000°` still is not.
  CHECK(!text.has_bearing);
  CHECK(is(text.bearing, "—"));
}

// The one line #433 changed, pinned by the one baseline that can tell the two
// functions apart on the screen.
//
// Every other pair in this file differs in latitude alone, and over a meridian
// `distance_mm()` and `great_circle_mm()` are 4.6 ppm apart — the gap between
// 11.132 mm and 11.131949 mm per 1e-7 degree — which no rendered string can
// show. So reverting `apps/src/navigation.cpp` to `distance_mm()` used to leave
// the whole suite green, and the readout's choice of function was pinned by
// nothing.
//
// 89°N 0°E to 89°N 180°E is the issue's own pair: two degrees of arc over the
// pole, 222 638 982 mm, against 352 223 437 mm the long way round the 89th
// parallel — the mean latitude is 89° exactly, so `cos_scaled()` returns the
// table entry unmixed and 20 037 600 000 × 18/1024 is the whole answer. Both
// are under the clamp and both go through the same `%u km` branch, so the
// difference reaches the glyphs.
void test_the_readout_measures_over_the_pole_and_not_around_it() {
  apps::NavState state;
  state.own = own_fix({890000000, 0});
  state.target = node_coordinate({890000000, 1800000000}, 1000);

  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.has_distance);
  if (!is(text.distance, "222 km")) {
    std::fprintf(stderr,
                 "%s:%d: distance is \"%s\", expected \"222 km\" — "
                 "\"352 km\" means the readout is back on distance_mm()\n",
                 __FILE__, __LINE__, text.distance);
    ++failures;
  }
}

void test_the_distance_changes_unit_where_a_person_would() {
  apps::NavState state;
  state.own = own_fix({0, 0});

  struct Case {
    std::int32_t latitude_e7;
    const char *expected;
  };
  // Latitudes read as an arc on the readout's own sphere: `great_circle_mm()`
  // over a meridian is 111 319.5 m per degree. These assert the *formatting*
  // boundaries; the geometry that produced the millimetres is
  // `tests/test_position.cpp`'s. Each is set a metre or so clear of the
  // rounding edge it is about, on purpose — the first pair used to sit within
  // five millimetres of one, and swapping the distance function underneath
  // them (#433) moved one across it. A boundary test should fail when the
  // *formatting* changes, not when the last millimetre does.
  const Case cases[] = {
      {80000, "890 m"},          // metres, while metres are what somebody walks
      {90000, "1.0 km"},         // the first kilometre, to one decimal
      {900000000, "> 1000 km"},  // the pole, past where the readout measures
  };
  for (const Case &c : cases) {
    state.target = node_coordinate({c.latitude_e7, 0}, 1000);
    const apps::NavText text = apps::format_navigation(state);
    CHECK(text.has_distance);
    if (!is(text.distance, c.expected)) {
      std::fprintf(stderr, "%s:%d: distance is \"%s\", expected \"%s\"\n",
                   __FILE__, __LINE__, text.distance, c.expected);
      ++failures;
    }
  }
}

void test_the_compass_points_are_centred_on_their_own_directions() {
  // 337.5°..22.5° is north. Off-by-a-half-sector here would put the needle's
  // label 22.5° away from the needle.
  CHECK(is(apps::cardinal_of(0), "N"));
  CHECK(is(apps::cardinal_of(2249), "N"));
  CHECK(is(apps::cardinal_of(2250), "NE"));
  CHECK(is(apps::cardinal_of(33750), "N"));
  CHECK(is(apps::cardinal_of(33749), "NW"));
  CHECK(is(apps::cardinal_of(35999), "N"));
  CHECK(is(apps::cardinal_of(9000), "E"));
  CHECK(is(apps::cardinal_of(18000), "S"));
  CHECK(is(apps::cardinal_of(27000), "W"));
}

void test_every_status_has_words() {
  const apps::NavStatus all[] = {
      apps::NavStatus::WaitingForGps,       apps::NavStatus::NoFix,
      apps::NavStatus::OwnPositionStale,    apps::NavStatus::OwnPositionDegraded,
      apps::NavStatus::NodeUnavailable,     apps::NavStatus::NodePositionUnknown,
      apps::NavStatus::NodePositionStale,   apps::NavStatus::OwnReceiverSilent,
      apps::NavStatus::Ready};
  // In both locales, because `l10n/strings.toml` is where the sentences live
  // now and a missing entry is a silent empty label rather than a link error.
  for (const apps::NavStatus status : all) {
    for (const l10n::Locale locale : {l10n::Locale::En, l10n::Locale::Ru}) {
      CHECK(apps::to_string(status, locale) != nullptr);
      CHECK(apps::to_string(status, locale)[0] != '\0');
    }
    CHECK(!is(apps::to_string(status, l10n::Locale::En),
              apps::to_string(status, l10n::Locale::Ru)));
  }
}

// A degraded own fix is usable and is not `Ready`.
//
// `core/include/attadipa/core/position.h:188` — "    Degraded,  // usable, with
// a caveat the interface must show". Both halves are load-bearing: folding it
// into `Ready` hides the caveat, and refusing on it hides a position the person
// can act on.
void test_a_degraded_own_fix_still_measures_and_still_says_so() {
  apps::NavState state;
  state.own = own_fix(kHere, core::PositionValidity::Degraded);
  state.target = node_coordinate({5100000, 10000000}, 1000);

  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.status_code == apps::NavStatus::OwnPositionDegraded);
  CHECK(text.status_code != apps::NavStatus::Ready);
  CHECK(text.has_distance);
  CHECK(text.has_bearing);
  CHECK(!is(text.distance, "—"));

  // The same state with a clean fix is the control: only the validity moved.
  state.own = own_fix(kHere, core::PositionValidity::Valid);
  CHECK(apps::format_navigation(state).status_code == apps::NavStatus::Ready);
}

// The two answers `availability.h` separates are two different sentences.
//
// `core/include/attadipa/core/availability.h:17` — "    Unsupported,    // no
// configuration of this device can provide it. Terminal." against
// `core/include/attadipa/core/availability.h:18` — "    Unprovisioned,  // a
// supported provider would give it; none is bound". Telling the second reader
// there is no receiver sends them to buy hardware they already own.
void test_an_unbound_provider_is_not_a_missing_receiver() {
  apps::NavState state;
  // With a node coordinate in hand, which is the case that used to lose this
  // sentence: a node states no fix type, so its own caveat is owed on every
  // coordinate that ever arrives, and testing it first left the wearer with
  // the quality of a number the screen is not drawing.
  state.target = node_coordinate({5100000, 10000000}, 3000);
  state.own.availability = core::Availability::Unsupported;
  const apps::NavText none = apps::format_navigation(state);

  state.own.availability = core::Availability::Unprovisioned;
  const apps::NavText unbound = apps::format_navigation(state);

  CHECK(none.caveat[0] != '\0');
  CHECK(unbound.caveat[0] != '\0');
  CHECK(!is(none.caveat, unbound.caveat));

  // And the node's caveat is still what a working watch says.
  apps::NavState fine = state;
  fine.own = own_fix({5100000, 10000000}, core::PositionValidity::Valid);
  CHECK(is(apps::format_navigation(fine).status,
           apps::to_string(apps::NavStatus::Ready)));
  CHECK(apps::format_navigation(fine).caveat[0] != '\0');
  CHECK(!is(apps::format_navigation(fine).caveat, none.caveat));
}

// Every string on the screen comes out of the catalogue, so switching the
// locale has to change every one of them that carries a word.
void test_the_readout_speaks_the_locale_it_was_given() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate({5100000, 10000000}, 4000);

  const apps::NavText en = apps::format_navigation(state);
  state.locale = l10n::Locale::Ru;
  const apps::NavText ru = apps::format_navigation(state);

  CHECK(!is(en.status, ru.status));
  CHECK(!is(en.title, ru.title));
  CHECK(!is(en.north, ru.north));
  CHECK(!is(en.caveat, ru.caveat));
  // The unit travels with the number: "4 m" is not "4 м".
  CHECK(!is(en.distance, ru.distance));
  // The bearing is digits and a degree sign, and those do not translate.
  CHECK(is(en.bearing, ru.bearing));
  // Nothing was cut on the longer language. `snprintf` always terminates, so
  // a NUL at the end proves nothing; the field has to still end in the same
  // characters the catalogue does. The first Russian render of this face lost
  // the last two words of the caveat and looked fine.
  CHECK(std::strlen(ru.caveat) < sizeof(ru.caveat) - 1);
  CHECK(std::strlen(ru.status) < sizeof(ru.status) - 1);
  CHECK(std::strlen(ru.cardinal) < sizeof(ru.cardinal) - 1);
  // The longest one there is: the age at its widest, in the longer language.
  apps::NavState longest = state;
  longest.target = node_coordinate({5100000, 10000000}, 0xFFFFFFFFU);
  const apps::NavText wide = apps::format_navigation(longest);
  CHECK(std::strlen(wide.caveat) < sizeof(wide.caveat) - 1);
}

}  // namespace

// ---- heading, ADR-0009 -----------------------------------------------------

// The one this file exists for. A node's compass is a true statement about a
// body that is not this one, and turning the needle with it draws the most
// plausible-looking wrong arrow this device can produce: everything on screen
// looks right and the wearer walks the wrong way by however far the node
// happens to be turned. ADR-0009 §3 decided this and nothing had to obey it
// until now.
void test_a_node_compass_never_turns_this_watch_s_needle() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate(core::Position{5100000, 10160000}, 3000);
  state.heading = watch_heading(9000);
  state.heading.frame = core::ReferenceFrame::NodeBody;
  state.heading.confidence = 100;  // perfect, and about the wrong body

  const apps::NavText text = apps::format_navigation(state);
  CHECK(text.has_bearing);
  CHECK(!text.has_arrow);
  CHECK(text.arrow_centideg == 0);
  CHECK(text.heading_centideg == 0);

  // And a course over ground is refused for the same reason: it is the
  // direction of travel, not the direction the case is pointing.
  state.heading.frame = core::ReferenceFrame::CourseOverGround;
  CHECK(!apps::format_navigation(state).has_arrow);
}

// The intended experience, ADR-0009 §5 row 1. The printed bearing stays against
// true north because a person checks it against a map; the needle is what turns.
void test_a_watch_that_knows_which_way_it_faces_points_at_the_wearer() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate(core::Position{5100000, 10160000}, 3000);
  const apps::NavText north_up = apps::format_navigation(state);
  CHECK(north_up.has_bearing);
  CHECK(!north_up.has_arrow);

  state.heading = watch_heading(9000);  // facing east
  const apps::NavText head_up = apps::format_navigation(state);
  CHECK(head_up.has_arrow);
  CHECK(head_up.bearing_centideg == north_up.bearing_centideg);
  CHECK(is(head_up.bearing, north_up.bearing));
  CHECK(is(head_up.cardinal, north_up.cardinal));
  CHECK(head_up.heading_centideg == 9000);
  CHECK(head_up.arrow_centideg ==
        core::relative_bearing(north_up.bearing_centideg, 9000));
}

// The subtraction wraps the short way round rather than through 65535. A
// bearing west of the heading is the case that catches an unsigned subtraction
// done without the extra turn, and it is not an edge case — it is half the
// circle.
void test_the_arrow_wraps_the_short_way_round() {
  CHECK(core::relative_bearing(1000, 35000) == 2000);
  CHECK(core::relative_bearing(35000, 1000) == 34000);
  CHECK(core::relative_bearing(0, 0) == 0);
  CHECK(core::relative_bearing(18000, 18000) == 0);
  CHECK(core::relative_bearing(0, 18000) == 18000);
}

// Three of the five validities are not `Valid`, and none of them may turn the
// needle. `Uncalibrated` is the interesting one: there is a number, and ADR-0009
// §5 row 7 says to draw it marked — which is a calibration surface this readout
// does not have yet, so until it does the honest fallback is the north-up
// readout rather than an arrow nothing has calibrated.
void test_a_heading_that_is_not_valid_does_not_turn_the_needle() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate(core::Position{5100000, 10160000}, 3000);

  const core::HeadingValidity refused[] = {
      core::HeadingValidity::Invalid, core::HeadingValidity::NoMotion,
      core::HeadingValidity::Stale, core::HeadingValidity::Uncalibrated};
  for (const core::HeadingValidity validity : refused) {
    state.heading = watch_heading(9000, validity);
    const apps::NavText text = apps::format_navigation(state);
    if (text.has_arrow) {
      std::fprintf(stderr, "%s turned the needle\n",
                   core::to_string(validity));
      CHECK(!text.has_arrow);
    }
    CHECK(text.has_bearing);
  }
}

// "Disturbed", without a sixth validity. ADR-0009 §6 carries confidence and
// leaves the threshold to the renderer, so a compass beside a running motor
// reports a number and a low confidence, and the readout goes back to north-up
// rather than swinging an arrow somebody would follow.
void test_a_disturbed_compass_falls_back_to_north_up() {
  apps::NavState state;
  state.own = own_fix(kHere);
  state.target = node_coordinate(core::Position{5100000, 10160000}, 3000);
  state.heading = watch_heading(9000, core::HeadingValidity::Valid, 10);
  CHECK(!apps::format_navigation(state).has_arrow);

  // The floor is the caller's, exactly as `target_stale_after` is.
  state.min_heading_confidence = 5;
  CHECK(apps::format_navigation(state).has_arrow);
}

// No bearing, no arrow. There is nothing to rotate, and an arrow drawn from a
// heading alone points at where the wearer is facing rather than at anything
// they are trying to reach.
void test_a_heading_with_nowhere_to_go_draws_no_arrow() {
  apps::NavState state;
  state.heading = watch_heading(9000);
  const apps::NavText text = apps::format_navigation(state);
  CHECK(!text.has_bearing);
  CHECK(!text.has_arrow);
  check_no_numbers(text);
}

// The default is the honest one, and it is the state both boards are in.
void test_the_default_heading_says_it_knows_nothing() {
  const core::Heading heading;
  CHECK(heading.validity == core::HeadingValidity::Invalid);
  CHECK(heading.confidence == 0);
  CHECK(!core::can_orient(heading, 0));
  CHECK(core::to_string(core::HeadingSource::Unknown) != nullptr);
  CHECK(core::to_string(core::ReferenceFrame::WatchBody) != nullptr);
  CHECK(core::to_string(core::HeadingValidity::Invalid) != nullptr);
}

// ADR-0009's Testable assertion, at the one place every caller passes through:
// "no configuration of inputs causes a wrist-relative arrow to be drawn from a
// `NodeBody` or `CourseOverGround` source."
//
// `WatchBody` is the default frame and `frame` is the one field a driver has no
// local evidence for -- it states which body the driver is bolted to. So the
// dangerous producer is not the one that fills nothing; it is the one that
// fills everything it *can* measure and leaves that one alone. Each source
// below is a real producer that would do exactly that.
void test_a_source_that_cannot_know_this_body_never_steers_it() {
  const core::HeadingSource cannot[] = {core::HeadingSource::Unknown,
                                        core::HeadingSource::GnssCourseOverGround,
                                        core::HeadingSource::RemoteSensor};
  for (const core::HeadingSource source : cannot) {
    core::Heading heading;  // frame deliberately never assigned
    heading.source = source;
    heading.centideg = 9000;
    heading.validity = core::HeadingValidity::Valid;
    heading.confidence = 100;
    CHECK(heading.frame == core::ReferenceFrame::WatchBody);
    CHECK(!core::can_orient(heading, 40));

    apps::NavState state;
    state.own = own_fix(kHere);
    state.target = node_coordinate(core::Position{5100000, 10160000}, 3000);
    state.heading = heading;
    const apps::NavText text = apps::format_navigation(state);
    CHECK(text.has_bearing);
    CHECK(!text.has_arrow);
  }

  // And the two that can, so the refusal above is the source and not something
  // else about a heading assembled this way.
  const core::HeadingSource can[] = {core::HeadingSource::Magnetometer,
                                     core::HeadingSource::SensorFusion};
  for (const core::HeadingSource source : can) {
    core::Heading heading;
    heading.source = source;
    heading.frame = core::ReferenceFrame::WatchBody;
    heading.centideg = 9000;
    heading.validity = core::HeadingValidity::Valid;
    heading.confidence = 100;
    CHECK(core::can_orient(heading, 40));
  }
}

int main() {
  test_a_watch_that_knows_nothing_says_so();
  test_a_receiver_that_answered_no_is_not_a_receiver_still_starting();
  test_a_local_coordinate_with_no_fix_behind_it_is_not_a_position();
  test_the_node_is_the_one_source_allowed_to_state_no_fix_and_still_count();
  test_ready_still_says_what_is_not_known();
  test_an_old_node_coordinate_leads_with_its_age();
  test_the_threshold_belongs_to_the_caller();
  test_a_dropped_link_is_reported_as_a_link_and_not_as_a_coordinate();
  test_a_receiver_that_stopped_answering_is_reported_as_a_receiver();
  test_a_node_that_never_said_where_it_is();
  test_a_coordinate_off_the_globe_is_refused_rather_than_saturated();
  test_a_stale_own_fix_is_neither_a_missing_one_nor_a_current_one();
  test_standing_on_it_is_a_measured_zero_and_not_a_direction();
  test_the_readout_measures_over_the_pole_and_not_around_it();
  test_the_distance_changes_unit_where_a_person_would();
  test_the_compass_points_are_centred_on_their_own_directions();
  test_every_status_has_words();
  test_a_degraded_own_fix_still_measures_and_still_says_so();
  test_an_unbound_provider_is_not_a_missing_receiver();
  test_the_readout_speaks_the_locale_it_was_given();
  test_a_node_compass_never_turns_this_watch_s_needle();
  test_a_watch_that_knows_which_way_it_faces_points_at_the_wearer();
  test_the_arrow_wraps_the_short_way_round();
  test_a_heading_that_is_not_valid_does_not_turn_the_needle();
  test_a_disturbed_compass_falls_back_to_north_up();
  test_a_heading_with_nowhere_to_go_draws_no_arrow();
  test_the_default_heading_says_it_knows_nothing();
  test_a_source_that_cannot_know_this_body_never_steers_it();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("navigation: all checks passed (host only — no receiver, no node)\n");
  return 0;
}
