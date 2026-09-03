#include <cstdio>
#include <cstring>

#include "attadipa/apps/navigation.h"

// Host tests for the navigation readout.
//
// The subject is not the arithmetic — `tests/test_position.cpp` owns distance
// and bearing. It is the rule the owner's brief states as
// `NoFix != stale != current != unknown`: which of seven sentences the screen
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
  // `distance_mm()` answers `kDistanceSaturated` for out-of-range as well as
  // for far away, so without this guard a hostile coordinate renders as a
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

void test_the_distance_changes_unit_where_a_person_would() {
  apps::NavState state;
  state.own = own_fix({0, 0});

  struct Case {
    std::int32_t latitude_e7;
    const char *expected;
  };
  // Latitudes from 11.132 mm per 1e-7 degree, the constant `geo.h` derives
  // from 111 320 m per degree. These assert the *formatting* boundaries; the
  // geometry that produced the millimetres is `tests/test_position.cpp`'s.
  const Case cases[] = {
      {79950, "890 m"},          // metres, while metres are what somebody walks
      {89832, "1.0 km"},         // the first kilometre, to one decimal
      {900000000, "> 1000 km"},  // the pole, past where distance_mm measures
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
      apps::NavStatus::OwnPositionStale,    apps::NavStatus::NodeUnavailable,
      apps::NavStatus::NodePositionUnknown, apps::NavStatus::NodePositionStale,
      apps::NavStatus::Ready};
  for (const apps::NavStatus status : all) {
    CHECK(apps::to_string(status) != nullptr);
    CHECK(apps::to_string(status)[0] != '\0');
  }
}

}  // namespace

int main() {
  test_a_watch_that_knows_nothing_says_so();
  test_a_receiver_that_answered_no_is_not_a_receiver_still_starting();
  test_a_local_coordinate_with_no_fix_behind_it_is_not_a_position();
  test_the_node_is_the_one_source_allowed_to_state_no_fix_and_still_count();
  test_ready_still_says_what_is_not_known();
  test_an_old_node_coordinate_leads_with_its_age();
  test_the_threshold_belongs_to_the_caller();
  test_a_dropped_link_is_reported_as_a_link_and_not_as_a_coordinate();
  test_a_node_that_never_said_where_it_is();
  test_a_coordinate_off_the_globe_is_refused_rather_than_saturated();
  test_a_stale_own_fix_is_neither_a_missing_one_nor_a_current_one();
  test_standing_on_it_is_a_measured_zero_and_not_a_direction();
  test_the_distance_changes_unit_where_a_person_would();
  test_the_compass_points_are_centred_on_their_own_directions();
  test_every_status_has_words();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("navigation: all checks passed (host only — no receiver, no node)\n");
  return 0;
}
