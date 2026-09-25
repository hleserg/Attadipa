// The board adapter's MeshCore send-argument rule, compiled from the shipping
// header rather than restated here: `firmware/main/meshcore_send_request.h` is
// the file `firmware/main/waveshare_board.cpp` includes, and
// `BoardMeshSink::send()` has no second copy of any bound below.
//
// #609 is the reason the file exists. The adapter bounded a private message at
// a literal 160 while the product contract and the worker underneath it stop
// at 128, so a 129-byte message was accepted by the adapter, refused
// synchronously by `meshcore_ble_send()` before anything was claimed or
// queued, and returned to the operator as `ErrorCode::OperationFailed` -- a
// radio failure that had not happened. Everything this header refuses is bad
// input, so the adapter answers `MeshSinkResult::Rejected` and the bridge maps
// that to `ErrorCode::BadInput`; `tests/test_debug.cpp` holds the bridge to
// that mapping for `MeshSend` itself.
//
// What no host test reaches is the ESP-IDF half: the adapter calling this, and
// the worker the adapter then calls. The call is covered by the firmware build;
// the radio behind it is NOT EXECUTED -- HARDWARE REQUIRED.

#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>

#include "meshcore_send_request.h"

namespace {

int failures = 0;

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

using attadipa::firmware::mesh_room_send_arguments_ok;
using attadipa::firmware::mesh_send_arguments_ok;
using attadipa::firmware::mesh_text_sendable;

// Any 32 bytes. The rule checks that a key is there, not what is in it.
const std::uint8_t key[32]{};

constexpr std::int64_t kWhen = 1'234'567'890;

void the_text_bound_is_the_product_constant_and_not_a_literal()
{
    // The bound is the contract, whatever the contract is. Writing 128 into
    // this test would let the two numbers drift apart again in exactly the way
    // #609 found, with a green test either side of the gap.
    CHECK(attadipa::core::kMeshTextBytes == 128);

    const std::string at_limit(attadipa::core::kMeshTextBytes, 'a');
    const std::string one_under(attadipa::core::kMeshTextBytes - 1, 'a');
    const std::string one_over(attadipa::core::kMeshTextBytes + 1, 'a');

    CHECK(mesh_send_arguments_ok(key, one_under.data(), one_under.size(), kWhen));
    CHECK(mesh_send_arguments_ok(key, at_limit.data(), at_limit.size(), kWhen));
    CHECK(!mesh_send_arguments_ok(key, one_over.data(), one_over.size(), kWhen));

    // The number the adapter used to carry. It is upstream's `MAX_TEXT_LEN`
    // and it is not this product's, and restoring it here is what this line
    // exists to fail on.
    const std::string upstreams_bound(160, 'a');
    CHECK(!mesh_send_arguments_ok(key, upstreams_bound.data(),
                                  upstreams_bound.size(), kWhen));

    // Every byte between the two numbers, not only the ends of the gap: the
    // defect lived in the interval and a two-point test would have passed over
    // a bound set anywhere inside it.
    for (std::size_t n = attadipa::core::kMeshTextBytes + 1; n <= 160; ++n) {
        const std::string over(n, 'a');
        CHECK(!mesh_send_arguments_ok(key, over.data(), over.size(), kWhen));
    }
}

void an_empty_or_absent_message_is_not_a_message()
{
    CHECK(!mesh_send_arguments_ok(key, "", 0, kWhen));
    CHECK(!mesh_send_arguments_ok(key, nullptr, 5, kWhen));
    CHECK(!mesh_send_arguments_ok(nullptr, "Hello", 5, kWhen));
    CHECK(mesh_send_arguments_ok(key, "Hello", 5, kWhen));
}

void a_nul_inside_the_span_is_refused_rather_than_silently_truncated()
{
    // `meshcore_ble_send()` copies the span and terminates it, so a NUL inside
    // would put a shorter message on the air than the one that was accepted --
    // and report success for it.
    const char embedded[] = {'H', 'i', '\0', 'y', 'o', 'u'};
    CHECK(!mesh_send_arguments_ok(key, embedded, sizeof(embedded), kWhen));
    CHECK(mesh_send_arguments_ok(key, embedded, 2, kWhen));
}

void the_bound_counts_bytes_and_not_characters()
{
    // Cyrillic is two UTF-8 bytes a letter, so 64 letters are exactly the
    // limit and 65 are over it while both are far short of 128 characters.
    // The host encoder is held to the same thing in tools/watch/selftest.py.
    std::string at_limit;
    for (int i = 0; i < 64; ++i) at_limit += "\xD0\xB0";  // U+0430 CYRILLIC A
    CHECK(at_limit.size() == attadipa::core::kMeshTextBytes);
    CHECK(mesh_send_arguments_ok(key, at_limit.data(), at_limit.size(), kWhen));

    std::string one_over = at_limit + "\xD0\xB0";
    CHECK(one_over.size() == attadipa::core::kMeshTextBytes + 2);
    CHECK(!mesh_send_arguments_ok(key, one_over.data(), one_over.size(), kWhen));

    // A four-byte code point straddling the limit is refused by its last byte
    // and not by its first, which is the case a character count gets wrong in
    // the other direction.
    std::string straddling(attadipa::core::kMeshTextBytes - 2, 'a');
    straddling += "\xF0\x9F\x99\x82";  // U+1F642, four bytes
    CHECK(straddling.size() == attadipa::core::kMeshTextBytes + 2);
    CHECK(!mesh_send_arguments_ok(key, straddling.data(), straddling.size(), kWhen));
}

void a_timestamp_the_frame_cannot_carry_is_refused_before_it_is_narrowed()
{
    constexpr std::int64_t kMaxEpoch = std::numeric_limits<std::uint32_t>::max();
    CHECK(mesh_send_arguments_ok(key, "Hello", 5, 0));
    CHECK(mesh_send_arguments_ok(key, "Hello", 5, kMaxEpoch));
    CHECK(!mesh_send_arguments_ok(key, "Hello", 5, kMaxEpoch + 1));
    CHECK(!mesh_send_arguments_ok(key, "Hello", 5, -1));
}

void the_room_send_answers_the_same_rule_about_the_same_field()
{
    // #609's other half: these two were separate copies of one check, and only
    // one of them carried the 160. Whatever the text rule is, both ask it.
    const std::string at_limit(attadipa::core::kMeshTextBytes, 'a');
    const std::string one_over(attadipa::core::kMeshTextBytes + 1, 'a');
    CHECK(mesh_room_send_arguments_ok(key, "pass", 4, at_limit.data(),
                                      at_limit.size(), kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, "pass", 4, one_over.data(),
                                       one_over.size(), kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, "pass", 4, nullptr, 5, kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, "pass", 4, "Hello", 5, -1));

    // The Room Server's own password bound, unchanged by #609 and checked so
    // that moving the text rule into a shared helper cannot have moved it.
    const std::string password_at_limit(15, 'p');
    const std::string password_over(16, 'p');
    CHECK(mesh_room_send_arguments_ok(key, password_at_limit.data(),
                                      password_at_limit.size(), "Hello", 5, kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, password_over.data(),
                                       password_over.size(), "Hello", 5, kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, "", 0, "Hello", 5, kWhen));
    CHECK(!mesh_room_send_arguments_ok(key, nullptr, 4, "Hello", 5, kWhen));
    CHECK(!mesh_room_send_arguments_ok(nullptr, "pass", 4, "Hello", 5, kWhen));

    const char embedded_password[] = {'p', '\0', 's', 's'};
    CHECK(!mesh_room_send_arguments_ok(key, embedded_password,
                                       sizeof(embedded_password), "Hello", 5, kWhen));
}

void the_text_predicate_is_the_one_both_of_them_use()
{
    // Not a third statement of the bound: this is the function the two above
    // delegate to, checked directly so a failure names the rule rather than
    // one of its callers.
    const std::string at_limit(attadipa::core::kMeshTextBytes, 'a');
    const std::string one_over(attadipa::core::kMeshTextBytes + 1, 'a');
    CHECK(mesh_text_sendable(at_limit.data(), at_limit.size()));
    CHECK(!mesh_text_sendable(one_over.data(), one_over.size()));
    CHECK(!mesh_text_sendable(nullptr, 1));
    CHECK(!mesh_text_sendable("Hello", 0));
}

}  // namespace

int main()
{
    the_text_bound_is_the_product_constant_and_not_a_literal();
    an_empty_or_absent_message_is_not_a_message();
    a_nul_inside_the_span_is_refused_rather_than_silently_truncated();
    the_bound_counts_bytes_and_not_characters();
    a_timestamp_the_frame_cannot_carry_is_refused_before_it_is_narrowed();
    the_room_send_answers_the_same_rule_about_the_same_field();
    the_text_predicate_is_the_one_both_of_them_use();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("meshcore send-request bounds: all checks passed\n");
    return 0;
}
