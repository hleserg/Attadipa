#include "meshcore_ble.h"
#include "meshcore_bond_recovery.h"
#include "meshcore_forget_outcome.h"
#include "meshcore_boot.h"
#include "meshcore_node_forget.h"
#include "meshcore_node_pin.h"
#include "meshcore_passkey.h"
#include "meshcore_passkey_outcome.h"
#include "meshcore_write_outcome.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "attadipa/link/meshcore_companion.h"
#include "attadipa/link/session_owner.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

namespace {

using attadipa::link::SessionCatchUp;
using attadipa::link::SessionMark;
using attadipa::link::SessionPhase;
using attadipa::link::SessionSnapshot;
using attadipa::link::SessionStep;

constexpr char kTag[] = "attadipa_mesh_ble";
// A contact list arrives as an unpaced burst: the node answered CMD_GET_CONTACTS
// with 148-byte RESP_CODE_CONTACT records ten milliseconds apart, and a
// sixteen-deep queue overran before the worker had run once. MEASURED on the
// bench 2026-08-28. Each event is one whole frame, so the depth is bounded
// heap, not a buffer that can grow.
//
// Since #317 this queue carries *only* data. The session lifecycle is not a
// message here at all — see session_owner.h — so a burst that fills this can
// cost frames, which the Companion protocol tolerates, and can no longer cost a
// disconnect, which it does not.
constexpr std::size_t kEventDepth = 48;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(500);
constexpr TickType_t kMeshCoreWriteDelay = pdMS_TO_TICKS(60);

static_assert(BLE_HS_CONN_HANDLE_NONE == attadipa::link::kNoSessionHandle,
              "the session record's no-connection value must be NimBLE's");
static_assert(BLE_HS_EOS == attadipa::firmware::kWriteStatusStackFailure,
              "the fatal transmit statuses must be NimBLE's");
static_assert(BLE_HS_ECONTROLLER == attadipa::firmware::kWriteStatusControllerFailure,
              "the fatal transmit statuses must be NimBLE's");

const ble_uuid128_t kServiceUuidValue = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
const ble_uuid128_t kRxUuidValue = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
const ble_uuid128_t kTxUuidValue = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);
const ble_uuid16_t kCccdUuidValue = BLE_UUID16_INIT(
    BLE_GATT_DSC_CLT_CFG_UUID16);

const ble_uuid_t* kServiceUuid = &kServiceUuidValue.u;
const ble_uuid_t* kRxUuid = &kRxUuidValue.u;
const ble_uuid_t* kTxUuid = &kTxUuidValue.u;
const ble_uuid_t* kCccdUuid = &kCccdUuidValue.u;

// What still travels through the queue: data, and the two requests the
// application makes of the transport. Everything the *radio* does is state, and
// state lives in the session record.
enum class EventKind : std::uint8_t {
    Wake,
    Configure,
    Deconfigure,
    Frame,
    OversizeFrame,
    Send,
    SendRoom,
    ForgetBond,
    ForgetNode,
};

struct Event {
    EventKind kind = EventKind::Wake;
    std::uint32_t generation = 0;
    std::uint16_t size = 0;
    std::uint32_t passkey = 0;
    // A Configure from the operator writes its passkey to NVS; the one boot
    // posts to replay a stored passkey does not, or every boot would rewrite
    // flash with what it just read.
    bool persist_passkey = false;
    // Who is waiting for this Configure or ForgetNode to finish, if anybody
    // is. Zero is the boot replay and the debug bridge, which post and do not
    // wait; the entry screen reserves a ticket first and reads the answer
    // back under it (meshcore_passkey_outcome.h).
    std::uint32_t ticket = 0;
    attadipa::core::WallTime timestamp{};
    std::array<std::uint8_t, attadipa::link::kMeshCoreFrameBytes> bytes{};
    std::array<std::uint8_t, 6> peer_prefix{};
    std::array<std::uint8_t, attadipa::core::kMeshPublicKeyBytes> room{};
    std::array<char, 16> password{};
    std::uint8_t password_length = 0;
    std::array<char, attadipa::core::kMeshTextBytes + 1> text{};
};

QueueHandle_t event_queue = nullptr;
attadipa::link::MeshCoreCompanion provider;
attadipa::core::MeshService service(provider);

// Two locks, and they are never nested. `snapshot_lock` guards the status any
// task may read; `session_lock` guards the BLE session that the NimBLE host
// task and the worker share. Nothing under `session_lock` calls into NimBLE and
// nothing under it takes the other lock, which is the whole of the locking
// discipline in this file.
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
attadipa::core::MeshStatus snapshot{};

portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;
attadipa::link::SessionOwner owner;

// Under `session_lock` for the same reason the session record is: the two
// tasks that touch it are the NimBLE host task, which records a conflict from
// the GAP callback, and the worker, which consumes it when the owner asks. The
// lock's rule holds -- nothing below calls into NimBLE while holding it.
attadipa::firmware::BondRecovery recovery;

// The policy header mirrors NimBLE's numbers; these bind the mirror to the
// definitions, so an upstream renumber fails the build instead of silently
// turning IGNORE into RETRY.
static_assert(attadipa::firmware::kRepeatPairingIgnore ==
                  BLE_GAP_REPEAT_PAIRING_IGNORE,
              "BLE_GAP_REPEAT_PAIRING_IGNORE moved");
static_assert(attadipa::firmware::kRepeatPairingRetry ==
                  BLE_GAP_REPEAT_PAIRING_RETRY,
              "BLE_GAP_REPEAT_PAIRING_RETRY moved");

// Held only across a handful of stores or one struct copy. A NimBLE call under
// this lock would be a re-entrant deadlock waiting to happen, because NimBLE
// can run a callback before the call that caused it has returned — so the
// sequence everywhere below is snapshot, call, conditional commit.
class SessionGuard {
public:
    SessionGuard() { taskENTER_CRITICAL(&session_lock); }
    ~SessionGuard() { taskEXIT_CRITICAL(&session_lock); }
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;
};

std::atomic<std::uint8_t> own_address_type{BLE_OWN_ADDR_RANDOM};
std::atomic_bool configured{false};
std::atomic_bool secure_pairing{false};
std::atomic_bool reconnect_allowed{false};
std::atomic_bool scan_report_seen{false};

// One outstanding mesh send, claimed where the caller can still be told.
//
// `mesh-send` used to answer MeshOk for anything xQueueSend accepted, and #315
// measured what that is worth: two requests, two successes, one ACK slot, and
// the second RESP_CODE_SENT counted as a malformed frame. The queue cannot
// answer the question the caller is actually asking -- whether *this* request
// is the operation the node and the provider are now tracking -- because the
// provider is worker-owned and the request runs on the USB task. So the slot is
// claimed here, before the post, and released by the worker once the provider
// says the operation reached a terminal outcome. A second request finds it
// taken and is refused synchronously, which is the whole of the fix as far as
// any caller can see.
std::atomic_bool send_claimed{false};

// WHICH NODE THIS WATCH IS ON, AND HOW IT STOPS BEING WHICHEVER ANSWERS FIRST.
//
// `advertises_meshcore()` matches the Companion service UUID or the name
// substring and connects to the first advertisement that arrives. With two
// nodes in range that is a coin toss the operator cannot see: the bench reached
// one node five times and the other four across nine runs, and the two
// disagreed about which room was reachable, so the swap changed the mesh and
// said nothing (docs/research/MESHCORE_T114_FIRST_CONTACT.md:54 "There are two
// MeshCore nodes in range").
//
// The identity is the node's public key, which arrives in RESP_CODE_SELF_INFO
// and which `MeshCoreCompanion` now reads. Neither identifier available here is
// permanent, and the key is chosen because it is the only one this repository
// has *measured* to outlive a connection
// (`docs/research/OWNER_DECISIONS.md:1182` -- "identity that outlives a
// connection"). A factory reset on the node
// regenerates it (`docs/research/MESHCORE_T114_FIRST_CONTACT.md:50` "a factory
// reset regenerates it"), which is a new identity by design.
//
// It is deliberately *not* chosen because the BLE address rotates. This file
// said that until #388's review, and no source in this repository carries it:
// the one address measured is random type 1
// (`docs/research/MESHCORE_T114_FIRST_CONTACT.md:47` "random"), read once and
// never read again, so whether it stays put or rotates is `UNKNOWN`
// (`docs/research/OWNER_DECISIONS.md:1177` -- "kind that rotates is therefore
// `UNKNOWN`").
// Nothing below depends on the answer; see `kRefusedNodeCooldownMs` in
// `firmware/main/meshcore_node_pin.h` for why the cooldown does not either.
//
// The rule is first-answer-then-pinned: an unprovisioned watch adopts the first
// node that answers RESP_CODE_SELF_INFO and writes it to NVS. That is the
// *first* Companion response, before CMD_DEVICE_QUERY and before any contact
// sync -- not the end of a handshake -- because that frame is where the key is
// and a watch that waited would already have talked to the node. After that a
// node whose key does not match is identified, refused and left alone.
//
// Choosing the node *up front* is a provisioning question, and this file still
// has no answer to it: nothing in the production image names a node before it
// answers. What the image has since #411 is the reverse. The entry screen's
// node field posts EventKind::ForgetNode, which deletes a recorded stale bond
// and clears the pin from `attadipa_mesh` and from the provider -- the order
// is in meshcore_node_forget.h -- so a watch whose node was factory-reset, or
// which is moved to another node, is re-pinned by its next adoption without
// `idf.py erase-flash`. The screen says which node is refused and which one is
// wanted, so that state is visible without a serial cable.
constexpr const char* kMeshNvsNamespace = "attadipa_mesh";
constexpr const char* kNodeKeyNvsKey    = "node";
// The passkey, once provisioned, so that a power cycle does not unprovision
// the watch (ADR-0018, fact 2). Plain NVS, like the pin and the bonds.
constexpr const char* kPasskeyNvsKey    = "passkey";

// The address of the peer this session is with, and the address of the last one
// refused, packed as type<<48 | the six address bytes. Atomics rather than a
// lock because the two writers are the NimBLE host task and the mesh worker,
// and `session_lock`'s rule is that nothing under it calls into NimBLE -- a
// refusal has to terminate a connection, so it cannot run under that lock.
std::atomic<std::uint64_t> connecting_addr{0};
std::atomic<std::uint64_t> refused_addr{0};
std::atomic<std::uint64_t> refused_until_ms{0};  // 64-bit, like the addresses
// The floor `after_refusal()` raises when a second address is refused while the
// first is still cooling down. See `firmware/main/meshcore_node_pin.h` --
// "struct RefusalState {" for why one slot alone is not a cooldown.
std::atomic<std::uint64_t> hold_all_until_ms{0};


std::uint64_t packed_addr(const ble_addr_t& addr)
{
    std::uint64_t packed = static_cast<std::uint64_t>(addr.type) << 48U;
    for (std::size_t i = 0; i < 6; ++i) {
        packed |= static_cast<std::uint64_t>(addr.val[i]) << (i * 8U);
    }
    return packed;
}

std::uint64_t uptime_ms()  // the full count: truncating it to 32 bits was #398
{
    return static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
}

// The three atomics read as one value. They are read and written separately, so
// a refusal landing between two of these loads can be seen half-applied; the
// worst it costs is one connection attempt either way, which the next
// advertisement corrects. A lock is not available here -- `session_lock`'s rule
// is that nothing under it calls into NimBLE, and this runs on the host task.
attadipa::firmware::RefusalState refusal_state()
{
    return {refused_addr.load(), refused_until_ms.load(),
            hold_all_until_ms.load()};
}

bool addr_is_refused(const ble_addr_t& addr)
{
    return attadipa::firmware::connect_is_refused(
        refusal_state(), packed_addr(addr), uptime_ms());
}

// Hex for a log line and for the screen. Not a formatter for the key itself --
// the first four bytes are what the bench reports identify nodes by
// (`5c62d9bc…`, `044e2de8…`), and eight characters is what fits beside the name.
void node_key_prefix(const attadipa::core::MeshPeerId& id, char (&out)[9])
{
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 4; ++i) {
        out[i * 2]     = kHex[id.public_key[i] >> 4U];
        out[i * 2 + 1] = kHex[id.public_key[i] & 0x0FU];
    }
    out[8] = '\0';
}

// The pin is read once, before the worker starts, and written only by the
// worker. Nothing else touches NVS in this file, so no lock is involved.
//
// THREE ANSWERS, NOT TWO. "Nothing is stored" is the ordinary state of a watch
// out of the box and it is not a fault; "NVS would not answer" is a fault, and
// a watch that reported it as the first one would silently adopt the next node
// it met -- which is the defect #304 exists to remove, reappearing through the
// storage layer.
enum class PinRead : std::uint8_t {
    Pinned,
    Unpinned,
    Unreadable,
};

PinRead load_node_pin(attadipa::core::MeshPeerId& out)
{
    nvs_handle_t handle{};
    const esp_err_t opened = nvs_open(kMeshNvsNamespace, NVS_READONLY, &handle);
    // A namespace nothing has written to yet does not exist.
    if (opened == ESP_ERR_NVS_NOT_FOUND) return PinRead::Unpinned;
    if (opened != ESP_OK) {
        ESP_LOGE(kTag, "MeshCore pin: nvs_open: %s", esp_err_to_name(opened));
        return PinRead::Unreadable;
    }
    std::size_t size = out.public_key.size();
    const esp_err_t err = nvs_get_blob(handle, kNodeKeyNvsKey,
                                       out.public_key.data(), &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return PinRead::Unpinned;
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "MeshCore pin: nvs_get_blob: %s", esp_err_to_name(err));
        return PinRead::Unreadable;
    }
    if (size != out.public_key.size()) {
        ESP_LOGE(kTag, "MeshCore pin is %u bytes, not %u; ignored",
                 static_cast<unsigned>(size),
                 static_cast<unsigned>(out.public_key.size()));
        return PinRead::Unreadable;
    }
    return PinRead::Pinned;
}

// `load_node_pin` for the passkey. What the value found is allowed to do is
// decided in `meshcore_passkey.h`, not here: this only reads.
attadipa::firmware::StoredPasskey load_passkey(std::uint32_t& out)
{
    using attadipa::firmware::StoredPasskey;
    nvs_handle_t handle{};
    const esp_err_t opened = nvs_open(kMeshNvsNamespace, NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return StoredPasskey::Absent;
    if (opened != ESP_OK) {
        ESP_LOGE(kTag, "MeshCore passkey: nvs_open: %s", esp_err_to_name(opened));
        return StoredPasskey::Unreadable;
    }
    const esp_err_t err = nvs_get_u32(handle, kPasskeyNvsKey, &out);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return StoredPasskey::Absent;
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "MeshCore passkey: nvs_get_u32: %s", esp_err_to_name(err));
        return StoredPasskey::Unreadable;
    }
    return StoredPasskey::Found;
}

bool store_passkey(std::uint32_t passkey)
{
    nvs_handle_t handle{};
    if (nvs_open(kMeshNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_u32(handle, kPasskeyNvsKey, passkey);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

// The off switch. Before the passkey outlived a boot, a power cycle was one;
// now `Deconfigure` has to be, or `mesh-disconnect` lasts until the next boot
// and a provisioned watch cannot be told to stop scanning without
// `erase-flash`, which takes the bonds, the pin and the time metadata too.
bool erase_passkey()
{
    nvs_handle_t handle{};
    const esp_err_t opened = nvs_open(kMeshNvsNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(handle, kPasskeyNvsKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool store_node_pin(const attadipa::core::MeshPeerId& id)
{
    nvs_handle_t handle{};
    if (nvs_open(kMeshNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, kNodeKeyNvsKey, id.public_key.data(),
                                 id.public_key.size());
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

// The pin's eraser, `erase_passkey()`'s shape on the other key. Its job is
// narrower than it looks: the next adoption overwrites the key anyway
// (`store_node_pin` is a set), so what this covers is a restart in the window
// between a forget and that adoption, which would otherwise put the old key
// back out of flash at boot.
bool erase_node_pin()
{
    nvs_handle_t handle{};
    const esp_err_t opened = nvs_open(kMeshNvsNamespace, NVS_READWRITE, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return true;
    if (opened != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(handle, kNodeKeyNvsKey);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}

bool claim_send()
{
    bool free_slot = false;
    return send_claimed.compare_exchange_strong(free_slot, true);
}

// The forget-bond in flight, and its answer once the worker has one. The
// request task reserves it and the worker completes it; the rules are in
// meshcore_forget_outcome.h, which the host tests compile.
attadipa::firmware::ForgetBondOperation forget_op;

// The same shape for the entry screen's passkey, and for the same reason: the
// arming and the flash write happen on this worker, and the screen that asked
// is on the LVGL task. meshcore_passkey_outcome.h, compiled by the host tests.
attadipa::firmware::PasskeyOperation passkey_op;

// And the same word again for forgetting the node: same screen, same two
// tasks, same ticket. meshcore_node_forget.h has the sequence it answers for.
attadipa::firmware::TicketedOperation<attadipa::firmware::ForgetNodeOutcome>
    forget_node_op;

attadipa::core::MonotonicTime now()
{
    return {static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
}

// NimBLE hands `cb_arg` back to the callback unchanged, so the generation that
// asked for an operation rides along with its answer. That is why a completion
// can tell whether it belongs to the connection that is live now without a
// lookup that would itself have to be raced for. A null argument reads as
// generation 0, which is never live.
void* generation_arg(std::uint32_t generation)
{
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(generation));
}

std::uint32_t generation_of(void* arg)
{
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(arg));
}

SessionSnapshot session_snapshot()
{
    SessionGuard guard;
    return owner.snapshot();
}

bool session_owns(std::uint32_t generation)
{
    SessionGuard guard;
    return owner.live(generation);
}

// Residual, named rather than engineered around (#316): xQueueSend byte-copies
// the whole Event into the queue's own storage, and a SendRoom Event carries the
// Room Server password by value. Both copies either side of the queue are
// cleared — the producer's here, the worker's in handle_send_room() — but the
// slot inside it is overwritten only when kEventDepth further events wrap to its
// index, which on a link that drops right after the login is never. That is RAM,
// reachable only by a memory dump or a disclosure bug, and outside the three
// exposures this issue closes; removing it means not carrying the secret through
// a by-value queue message at all.
bool post(const Event& event)
{
    return event_queue != nullptr && xQueueSend(event_queue, &event, 0) == pdTRUE;
}

// A doorbell, not a message. The record is authoritative; this only shortens
// the wait. Its failure is ignored deliberately: the one condition that makes
// it fail is a full queue, which is also the one condition under which the
// worker already has work and is about to read the record anyway. Losing it
// costs at most kPollTicks of latency and can never cost a transition.
void wake_worker()
{
    (void)post(Event{EventKind::Wake});
}

void publish(const SessionSnapshot& session)
{
    attadipa::core::MeshStatus next = provider.status();
    // From the same coherent read as everything else about this session, so a
    // published MTU always belongs to the connection it is published with.
    next.mtu = session.mtu;
    if (!configured.load()) {
        next.availability = attadipa::core::Availability::Unprovisioned;
    }
    taskENTER_CRITICAL(&snapshot_lock);
    snapshot = next;
    taskEXIT_CRITICAL(&snapshot_lock);
}

int gap_event(ble_gap_event* event, void* arg);

void start_scan()
{
    if (session_snapshot().stack_readies == 0 || !configured.load() ||
        !reconnect_allowed.load() || ble_gap_disc_active()) {
        return;
    }
    ble_gap_disc_params params{};
    params.passive = 0;
    // An active scan may receive the MeshCore name or service UUID only in the
    // scan response. Do not discard it after the preceding advertisement.
    params.filter_duplicates = 0;
    const int rc = ble_gap_disc(own_address_type.load(), BLE_HS_FOREVER, &params,
                                gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "start scan failed: %d", rc);
        {
            SessionGuard guard;
            owner.fault();
        }
        wake_worker();
    } else {
        scan_report_seen.store(false);
        ESP_LOGI(kTag, "scanning for MeshCore Companion service");
    }
}

bool advertises_meshcore(const ble_gap_disc_desc& disc)
{
    ble_hs_adv_fields fields{};
    if (ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data) != 0) {
        return false;
    }
    for (std::uint8_t i = 0; i < fields.num_uuids128; ++i) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, kServiceUuid) == 0) {
            return true;
        }
    }
    return fields.name != nullptr && fields.name_len >= 8 &&
           std::search(fields.name, fields.name + fields.name_len,
                       reinterpret_cast<const std::uint8_t*>("MeshCore"),
                       reinterpret_cast<const std::uint8_t*>("MeshCore") + 8) !=
               fields.name + fields.name_len;
}

// The stack itself failed rather than a session — a host reset, an address it
// would not configure, a scan it would not start. Recorded rather than queued,
// so the worker learns of it even if every queue slot is holding a frame.
void stack_fault(const char* what, int rc)
{
    ESP_LOGE(kTag, "%s: %d", what, rc);
    {
        SessionGuard guard;
        owner.fault();
    }
    wake_worker();
}

// A failure that ends the session it names. A generation that is no longer live
// records nothing at all: a callback belonging to a connection that has already
// been replaced must not fault, or terminate, its replacement. That is the
// whole reason the generation is carried through NimBLE's own callback
// argument.
void disconnect_fault(std::uint32_t generation, const char* step, int rc)
{
    std::uint16_t connection = attadipa::link::kNoSessionHandle;
    bool ours = false;
    {
        SessionGuard guard;
        ours = owner.live(generation);
        if (ours) {
            owner.fault();
            connection = owner.connection();
        }
    }
    if (!ours) {
        ESP_LOGD(kTag, "%s failed for a session that is already over: %d", step, rc);
        return;
    }
    ESP_LOGE(kTag, "%s failed: %d", step, rc);
    reconnect_allowed.store(false);
    wake_worker();
    if (connection != attadipa::link::kNoSessionHandle) {
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}

int subscribe_done(std::uint16_t, const ble_gatt_error* error,
                   ble_gatt_attr*, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status != 0) {
        disconnect_fault(generation, "subscribe", error->status);
        return 0;
    }
    bool established = false;
    std::uint16_t mtu = 0;
    {
        SessionGuard guard;
        established = owner.ready(generation);
        mtu = owner.snapshot().mtu;
    }
    if (!established) return 0;
    ESP_LOGI(kTag, "Companion GATT ready, MTU %u", static_cast<unsigned>(mtu));
    wake_worker();
    return 0;
}

int descriptor_discovered(std::uint16_t conn, const ble_gatt_error* error,
                          std::uint16_t, const ble_gatt_dsc* descriptor,
                          void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status == 0 && descriptor != nullptr) {
        if (ble_uuid_cmp(&descriptor->uuid.u, kCccdUuid) == 0) {
            SessionGuard guard;
            (void)owner.set_cccd_handle(generation, descriptor->handle);
        }
        return 0;
    }
    const SessionSnapshot session = session_snapshot();
    if (session.generation != generation || session.phase == SessionPhase::Ended) {
        return 0;
    }
    if (error->status != BLE_HS_EDONE || session.cccd_handle == 0) {
        disconnect_fault(generation, "discover CCCD", error->status);
        return 0;
    }
    const std::uint8_t enable[] = {1, 0};
    const int rc = ble_gattc_write_flat(conn, session.cccd_handle, enable,
                                        sizeof(enable), subscribe_done, arg);
    if (rc != 0) disconnect_fault(generation, "write CCCD", rc);
    return 0;
}

int tx_discovered(std::uint16_t conn, const ble_gatt_error* error,
                  const ble_gatt_chr* characteristic, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status == 0 && characteristic != nullptr) {
        SessionGuard guard;
        (void)owner.set_tx_handle(generation, characteristic->val_handle);
        return 0;
    }
    const SessionSnapshot session = session_snapshot();
    if (session.generation != generation || session.phase == SessionPhase::Ended) {
        return 0;
    }
    if (error->status != BLE_HS_EDONE || session.tx_handle == 0) {
        disconnect_fault(generation, "discover TX characteristic", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_all_dscs(conn, session.tx_handle,
                                            session.service_end,
                                            descriptor_discovered, arg);
    if (rc != 0) disconnect_fault(generation, "start CCCD discovery", rc);
    return 0;
}

int rx_discovered(std::uint16_t conn, const ble_gatt_error* error,
                  const ble_gatt_chr* characteristic, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status == 0 && characteristic != nullptr) {
        SessionGuard guard;
        (void)owner.set_rx_handle(generation, characteristic->val_handle);
        return 0;
    }
    const SessionSnapshot session = session_snapshot();
    if (session.generation != generation || session.phase == SessionPhase::Ended) {
        return 0;
    }
    if (error->status != BLE_HS_EDONE || session.rx_handle == 0) {
        disconnect_fault(generation, "discover RX characteristic", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_chrs_by_uuid(conn, session.service_start,
                                                session.service_end, kTxUuid,
                                                tx_discovered, arg);
    if (rc != 0) disconnect_fault(generation, "start TX discovery", rc);
    return 0;
}

int service_discovered(std::uint16_t conn, const ble_gatt_error* error,
                       const ble_gatt_svc* discovered, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status == 0 && discovered != nullptr) {
        SessionGuard guard;
        (void)owner.set_service_range(generation, discovered->start_handle,
                                      discovered->end_handle);
        return 0;
    }
    const SessionSnapshot session = session_snapshot();
    if (session.generation != generation || session.phase == SessionPhase::Ended) {
        return 0;
    }
    if (error->status != BLE_HS_EDONE || session.service_start == 0) {
        disconnect_fault(generation, "discover Companion service", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_chrs_by_uuid(conn, session.service_start,
                                                session.service_end, kRxUuid,
                                                rx_discovered, arg);
    if (rc != 0) disconnect_fault(generation, "start RX discovery", rc);
    return 0;
}

int mtu_done(std::uint16_t conn, const ble_gatt_error* error,
             std::uint16_t mtu, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    if (error->status != 0) {
        disconnect_fault(generation, "MTU exchange", error->status);
        return 0;
    }
    bool ours = false;
    {
        SessionGuard guard;
        ours = owner.set_mtu(generation, mtu);
    }
    if (!ours) return 0;
    const int rc = ble_gattc_disc_svc_by_uuid(conn, kServiceUuid,
                                               service_discovered, arg);
    if (rc != 0) disconnect_fault(generation, "start service discovery", rc);
    return 0;
}

int write_done(std::uint16_t, const ble_gatt_error* error,
               ble_gatt_attr*, void* arg)
{
    const std::uint32_t generation = generation_of(arg);
    bool accepted = false;
    {
        SessionGuard guard;
        accepted = owner.write_completed(generation, error->status);
    }
    if (!accepted) {
        // A completion from a connection that has already been torn down. It
        // used to clear a flag that by then belonged to a live session — and,
        // when the queue was full, to fault the whole subsystem rather than
        // report a lost result. Neither is this session's business.
        ESP_LOGD(kTag, "write completion from a session that is over: %d",
                 error->status);
        return 0;
    }
    wake_worker();
    return 0;
}

// A bond the node no longer honours, recorded so the owner can forget it.
//
// `ble_gap_conn_find` runs before the guard: nothing calls into NimBLE under
// `session_lock`. False means the peer could not be identified, which is
// recorded as nothing at all -- fail-closed, so the owner is never offered a
// deletion aimed at a bond this firmware cannot name.
bool record_stale_bond(std::uint16_t connection, const char* cause)
{
    ble_gap_conn_desc desc{};
    attadipa::firmware::BondIdentity peer{};
    if (ble_gap_conn_find(connection, &desc) == 0) {
        peer.type = desc.peer_id_addr.type;
        std::memcpy(peer.address.data(), desc.peer_id_addr.val,
                    peer.address.size());
        peer.valid = true;
    }
    bool recorded = false;
    {
        SessionGuard guard;
        recovery.record(peer);
        recorded = recovery.recovery_required();
    }
    // The last three octets only. A resolvable private address is not a
    // credential, but it is the peer's identity and a log is an artefact that
    // leaves the bench.
    ESP_LOGE(kTag,
             "MeshCore bond is stale (%s): type=%u addr=xx:xx:xx:%02X:%02X:%02X."
             " The bond is kept. Mesh stays down until the owner forgets it"
             " (mesh-forget-bond).",
             cause, static_cast<unsigned>(peer.type),
             static_cast<unsigned>(peer.address[2]),
             static_cast<unsigned>(peer.address[1]),
             static_cast<unsigned>(peer.address[0]));
    if (!recorded) {
        ESP_LOGE(kTag, "the conflicting peer could not be identified; "
                       "no bond can be offered for recovery");
    }
    return recorded;
}

int gap_event(ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (!scan_report_seen.exchange(true)) {
            ESP_LOGI(kTag, "received BLE advertising report");
        }
        if (!configured.load() || !reconnect_allowed.load()) return 0;
        if (!advertises_meshcore(event->disc)) return 0;
        // A node whose key is not the pinned one was refused a moment ago, and
        // it is still advertising. Connecting again would refuse it again, for
        // as long as it is in range: the scan has to skip it, not the
        // handshake. Only its address is remembered, because the key is not
        // knowable from an advertisement.
        if (addr_is_refused(event->disc.addr)) return 0;
        ESP_LOGI(kTag, "matched MeshCore advertisement");
        if (ble_gap_disc_cancel() != 0) return 0;
        connecting_addr.store(packed_addr(event->disc.addr));
        std::uint32_t generation = 0;
        {
            SessionGuard guard;
            generation = owner.peer_arriving();
        }
        wake_worker();
        // From here on every callback for this connection carries its
        // generation, because that is what NimBLE was handed as `cb_arg`.
        if (ble_gap_connect(own_address_type.load(), &event->disc.addr, 30000,
                            nullptr, gap_event, generation_arg(generation)) != 0) {
            {
                SessionGuard guard;
                (void)owner.ended(generation);
            }
            wake_worker();
            start_scan();
        }
        return 0;
    }
    case BLE_GAP_EVENT_CONNECT: {
        const std::uint32_t generation = generation_of(arg);
        const std::uint16_t conn = event->connect.conn_handle;
        if (event->connect.status != 0) {
            {
                SessionGuard guard;
                (void)owner.ended(generation);
            }
            wake_worker();
            start_scan();
            return 0;
        }
        bool ours = false;
        {
            SessionGuard guard;
            ours = owner.connected(generation, conn);
        }
        if (!ours) {
            // Nobody is waiting for this connection: the session it belongs to
            // was ended while it was being made. Close it rather than leave a
            // link nothing owns.
            ESP_LOGW(kTag, "closing a connection whose session is already over");
            (void)ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        if (!configured.load()) {
            ESP_LOGI(kTag, "closing connection after local stop");
            (void)ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        if (secure_pairing.load()) {
            ESP_LOGI(kTag, "MeshCore connected; starting BLE security");
            if (ble_gap_security_initiate(conn) != 0) {
                disconnect_fault(generation, "start pairing", BLE_HS_EAUTHEN);
            }
        } else if (ble_gattc_exchange_mtu(conn, mtu_done,
                                          generation_arg(generation)) != 0) {
            disconnect_fault(generation, "start MTU exchange", BLE_HS_EAPP);
        }
        return 0;
    }
    case BLE_GAP_EVENT_ENC_CHANGE: {
        const std::uint32_t generation = generation_of(arg);
        if (event->enc_change.status != 0) {
            // `PIN or Key Missing`, and nothing else. This is the node saying
            // it holds no key for the LTK this watch just encrypted with --
            // #325's factory-reset node, seen from the central side, and the
            // only failure that indicts the bond itself rather than the
            // attempt. Every other pairing failure keeps the old path: a
            // fault, no record, nothing offered for deletion.
            if (event->enc_change.status ==
                BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {
                (void)record_stale_bond(event->enc_change.conn_handle,
                                        "the node has no key for it");
            }
            disconnect_fault(generation, "pairing", event->enc_change.status);
            return 0;
        }
        {
            SessionGuard guard;
            recovery.pairing_succeeded();
        }
        if (ble_gattc_exchange_mtu(event->enc_change.conn_handle, mtu_done,
                                   generation_arg(generation)) != 0) {
            disconnect_fault(generation, "start MTU exchange", BLE_HS_EAPP);
        }
        return 0;
    }
    case BLE_GAP_EVENT_NOTIFY_RX: {
        const std::uint32_t generation = generation_of(arg);
        // One coherent read of the session. The three facts that have to agree
        // — this is the live session, it is subscribed, and this is its TX
        // handle — used to be three plain globals a disconnect could clear
        // between, which is the torn read #317 is about. A notification queued
        // during a previous connection is dropped here rather than parsed.
        const SessionSnapshot session = session_snapshot();
        if (session.generation != generation ||
            session.phase != SessionPhase::Ready ||
            event->notify_rx.conn_handle != session.connection ||
            event->notify_rx.attr_handle != session.tx_handle) {
            return 0;
        }
        const std::uint16_t length = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (length > attadipa::link::kMeshCoreFrameBytes) {
            // The node is a peer, not a trusted source, and a third party on
            // the air can provoke what it sends (MESHCORE_PARSER_BOUNDS.md §5).
            // Drop the frame before any copy and count it; tearing the link
            // down here would let one malformed notification end the session --
            // and disconnect_fault() also clears reconnect_allowed, so it would
            // end mesh for the whole boot.
            (void)post(Event{EventKind::OversizeFrame});
            return 0;
        }
        Event incoming{EventKind::Frame};
        incoming.generation = generation;
        incoming.size = length;
        if (os_mbuf_copydata(event->notify_rx.om, 0, length,
                             incoming.bytes.data()) != 0 || !post(incoming)) {
            // Same reasoning as the over-size case above, and it was learned the
            // hard way: this used to call disconnect_fault(), so one burst that
            // outran the worker cleared reconnect_allowed and ended mesh for the
            // whole boot. MEASURED on the bench 2026-08-28 -- two dropped
            // contact records took the link down at 8.3 s and nothing rescanned
            // for the remaining four minutes. A full queue is backpressure, not
            // a broken subsystem. The Companion protocol tolerates a lost frame:
            // a contact record is re-sent by the next CMD_GET_CONTACTS and the
            // sync boundary still arrives, and a lost push is one message.
            {
                SessionGuard guard;
                owner.frame_dropped();
            }
            ESP_LOGW(kTag, "dropped a Companion frame: op=0x%02X len=%u",
                     static_cast<unsigned>(incoming.bytes[0]),
                     static_cast<unsigned>(length));
        }
        return 0;
    }
    case BLE_GAP_EVENT_MTU: {
        SessionGuard guard;
        (void)owner.set_mtu(generation_of(arg), event->mtu.value);
        return 0;
    }
    case BLE_GAP_EVENT_DISCONNECT: {
        ESP_LOGW(kTag, "MeshCore disconnected: %d", event->disconnect.reason);
        {
            SessionGuard guard;
            (void)owner.ended(generation_of(arg));
        }
        // The worker learns this from the record, not from this doorbell, which
        // is precisely the loss #317's second comment reported: this event used
        // to be a queue entry whose failure was discarded.
        wake_worker();
        start_scan();
        return 0;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE:
        start_scan();
        return 0;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // The peer holds no key for the bond this watch holds -- the ordinary
        // cause is the node being factory-reset or reflashed, which the test
        // fleet has already done once. Nothing is deleted here. Returning
        // RETRY would mean deleting first, and NimBLE #2206 is that the
        // deletion lands before Phase 2 authentication, so a peer that merely
        // sent a Pairing Request would have evicted the bond.
        //
        // Unreachable on this device: `ble_sm_chk_repeat_pairing()`
        // (`host/src/ble_sm.c:990`) is called from `ble_sm_pair_req_rx()` alone
        // (`host/src/ble_sm.c:1956`, call at `:2079`), and a central never
        // receives a Pairing
        // Request. Kept because the callback still owes NimBLE an answer, and
        // because falling through would give the default one. The trigger this
        // firmware actually runs on is in BLE_GAP_EVENT_ENC_CHANGE below.
        (void)record_stale_bond(event->repeat_pairing.conn_handle,
                                "the peer asked to pair again");
        // Fail closed and say so. Without this the stack silently drops the
        // request and the link hangs until something else times out, which is
        // exactly the silent permanent failure #325 reports. disconnect_fault
        // stops reconnecting, so the reconnect loop this would otherwise spin
        // does not happen either; forget_bond re-arms it.
        disconnect_fault(generation_of(arg), "pairing with an existing bond",
                         BLE_HS_EAUTHEN);
        return attadipa::firmware::kRepeatPairingIgnore;
    }
    default:
        return 0;
    }
}

// The frame transcript this task's evidence rests on: every Companion frame
// that crosses the link, by direction, opcode and length, with its bytes.
// There is no BLE sniffer on this bench and the host is not a party to this
// link, so the firmware is the only place a transcript can come from.
// Bounded by construction -- a frame is at most kMeshCoreFrameBytes
// (MESHCORE_BLE_FRAME_CAPACITY.md section 3) and an over-size notification is
// dropped in the callback and never reaches either call site.
// A transcript is a debugging aid and is not worth a credential. How much of a
// frame may be printed is the encoder's decision, not this function's: see
// attadipa::link::meshcore_loggable_prefix. CMD_SEND_LOGIN stops after the
// public room key, and the header still records that bytes were withheld, so a
// redacted capture cannot be mistaken for a short frame.
void log_frame(const char* direction, const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0) return;
    const std::size_t printable = attadipa::link::meshcore_loggable_prefix(data, size);
    if (printable < size) {
        ESP_LOGI(kTag, "%s op=0x%02X len=%u (%u bytes redacted: credential)",
                 direction, static_cast<unsigned>(data[0]),
                 static_cast<unsigned>(size),
                 static_cast<unsigned>(size - printable));
    } else {
        ESP_LOGI(kTag, "%s op=0x%02X len=%u", direction,
                 static_cast<unsigned>(data[0]), static_cast<unsigned>(size));
    }
    if (printable != 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(kTag, data, printable, ESP_LOG_INFO);
    }
}

// pump_tx pops a frame onto its stack and returns from four places. One of the
// frames that passes through is CMD_SEND_LOGIN, so the copy is cleared on every
// exit rather than at one of them. Clearing right after ble_gattc_write_flat()
// returns is safe because that call copies the flat buffer into an mbuf before
// it returns, which is the same contract that lets this frame be a stack local
// at all: the transmit path is not reading these bytes afterwards.
struct FrameScrub {
    attadipa::link::MeshCoreFrame& frame;
    ~FrameScrub()
    {
        frame.bytes.fill(0);
        frame.size = 0;
    }
};

void pump_tx(const SessionSnapshot& session)
{
    if (session.phase != SessionPhase::Ready || session.write_in_flight ||
        session.connection == attadipa::link::kNoSessionHandle ||
        session.rx_handle == 0) {
        return;
    }
    attadipa::link::MeshCoreFrame frame{};
    if (!provider.next_tx(frame)) return;
    FrameScrub scrub{frame};
    const std::uint16_t payload_limit = session.mtu > 3
                                            ? session.mtu - 3
                                            : 0;
    if (frame.size > payload_limit) {
        disconnect_fault(session.generation,
                         "frame exceeds negotiated ATT payload", BLE_HS_EMSGSIZE);
        return;
    }
    // Claim the slot before the call, and only if the generation the snapshot
    // was taken from still owns the transport. This is the conditional commit:
    // between the snapshot above and here the peer may have gone, and a write
    // to the handle it left behind is the defect. Claiming first also survives
    // NimBLE running the completion before the call that caused it returns.
    //
    // What this does *not* close, said plainly rather than left for a reviewer
    // to find: the peer can still go away between the claim and the call below,
    // because the call is made outside the lock — which ADR-0015 requires, since
    // NimBLE can re-enter. The write then goes to a handle NimBLE has already
    // invalidated, and NimBLE rejects it (`BLE_HS_ENOTCONN`), which lands in the
    // rc != 0 path. The only way it could reach a *different* session is if a
    // disconnect, a scan, a connect and a rediscovery all completed within these
    // few instructions and the stack reissued the same connection handle. That
    // is not a race a lock could close without holding one across a stack call.
    bool claimed = false;
    {
        SessionGuard guard;
        claimed = owner.write_submitted(session.generation);
    }
    if (!claimed) {
        // The session ended under us. The frame is lost with it, which is
        // correct: the reconnect path calls provider.begin() and the Companion
        // handshake starts again from CMD_APP_START.
        ESP_LOGW(kTag, "dropped an outgoing frame: the session ended first");
        {
            SessionGuard guard;
            owner.frame_dropped();
        }
        return;
    }
    const int rc = ble_gattc_write_flat(session.connection, session.rx_handle,
                                         frame.bytes.data(), frame.size,
                                         write_done,
                                         generation_arg(session.generation));
    if (rc != 0) {
        {
            SessionGuard guard;
            (void)owner.write_completed(session.generation, rc);
        }
        // Which of the two fault domains this is. Almost every immediate
        // rejection is session-scoped -- the peer went away in the window
        // documented above, or the host is out of mbufs -- and #335 is what it
        // cost to treat them all as fatal: disconnect_fault() clears
        // reconnect_allowed, so an ordinary disappearance left the transport
        // unable to come back without another Configure or a reboot, while the
        // *same* status arriving through write_done() cost one session and
        // recovered.
        //
        // Recycling needs no code of its own. The completion recorded above is
        // reconciled on the worker's next pass into handle_write_result(),
        // which terminates the connection; the GAP disconnect then ends the
        // generation and start_scan() runs. That is the code the async arm has
        // always recovered through, so the two arms are one outcome by
        // construction rather than two that happen to agree. The frame is lost
        // with the session -- counted, not merely logged -- which is correct
        // here for the same reason it is correct when the claim above fails:
        // the reconnect calls provider.begin() and the Companion handshake
        // starts again from CMD_APP_START.
        switch (attadipa::firmware::classify_write_failure(rc)) {
        case attadipa::firmware::WriteOutcome::Recycle:
            ESP_LOGW(kTag, "Companion write rejected (%d); recycling the session", rc);
            {
                SessionGuard guard;
                owner.frame_dropped();
            }
            // Outside the guard: SessionGuard is a critical section, and the
            // wake posts to a queue.
            wake_worker();
            break;
        case attadipa::firmware::WriteOutcome::Fault:
            disconnect_fault(session.generation, "write Companion frame", rc);
            break;
        }
    } else {
        log_frame("TX", frame.bytes.data(), frame.size);
    }
}

void handle_write_result(std::int32_t result, std::uint32_t generation)
{
    // A completion outlives the session that submitted it: the worker may not
    // run again until the connection is gone, and `write_completions` is a
    // count the worker owes rather than a message it can miss. Neither half of
    // what follows means anything for a session that has been replaced -- the
    // delay paces a transmitter that no longer exists, and the terminate below
    // would tear down whichever connection inherited the handle rather than the
    // one whose write failed, which is the bug generations exist to prevent.
    const SessionSnapshot session = session_snapshot();
    if (session.generation != generation || session.phase == SessionPhase::Ended) {
        return;
    }
    if (result == 0) {
        vTaskDelay(kMeshCoreWriteDelay);
        return;
    }
    // A write that failed is the peer not answering, not a broken subsystem,
    // and calling fault() here is what wedged the session on the bench. ATT
    // allows a write-with-response 30 s before the host must tear the link
    // down, so this arrives immediately before BLE_GAP_EVENT_DISCONNECT -- and
    // Faulted refuses PeerArriving and PeerEstablished alike, so the reconnect
    // that follows could never re-establish the session. Terminating routes the
    // failure into the disconnect path, which is the single place that
    // recovers; when the link is already gone that path is running anyway.
    ESP_LOGW(kTag, "Companion write failed: %" PRId32 "; recycling the session",
             result);
    if (session.connection != attadipa::link::kNoSessionHandle) {
        (void)ble_gap_terminate(session.connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// Everything the worker still owes the link model, taken from the record rather
// than from the queue. This runs first on every pass, so a frame is never
// handed to a provider whose session state is one disconnect behind.
//
// The write result it found is returned rather than acted on, because acting on
// it means pausing 60 ms and that pause belongs after the queued frame, where
// the WriteDone queue entry used to sit. A contact burst arrives faster than the
// worker drains it; a pacing delay that jumped the queue would make that worse.
SessionCatchUp apply_lifecycle(SessionMark& applied)
{
    SessionSnapshot session{};
    SessionCatchUp catch_up{};
    {
        SessionGuard guard;
        session = owner.snapshot();
        catch_up = attadipa::link::reconcile(applied, session);
        owner.note_coalesced(catch_up.coalesced);
    }
    applied = attadipa::link::mark_of(session);

    if (catch_up.coalesced != 0) {
        ESP_LOGW(kTag, "%" PRIu32 " lifecycle transitions coalesced (%" PRIu32
                       " total), %" PRIu32 " Companion frames dropped so far",
                 catch_up.coalesced,
                 session.coalesced_lifecycle + catch_up.coalesced,
                 session.dropped_frames);
    }

    for (std::uint8_t i = 0; i < catch_up.count; ++i) {
        switch (catch_up.steps[i]) {
        case SessionStep::StackReady:
            provider.begin(now());
            if (configured.load()) start_scan();
            break;
        case SessionStep::Fault:
            provider.fault(now());
            break;
        case SessionStep::PeerArriving:
            provider.peer_arriving(now());
            break;
        case SessionStep::Ready:
            provider.connected(now());
            break;
        case SessionStep::Disconnected:
            provider.disconnected(now());
            // disconnected() can only apply PeerGone to a live link, so a
            // fault recorded while the peer was still there leaves the
            // phase Faulted -- and Faulted refuses both PeerArriving and
            // PeerEstablished. MEASURED on the bench 2026-08-27: the link
            // came back with MTU 247 and not one Companion frame was sent
            // again for the rest of the boot. begin() is the only call
            // that resets the link model, and it is made only where a
            // reconnect will actually be attempted: disconnect_fault()
            // clears reconnect_allowed precisely so a broken subsystem is
            // not retried forever, and start_scan() honours it.
            if (reconnect_allowed.load()) provider.begin(now());
            break;
        }
    }

    return catch_up;
}

// Both return whether the provider took ownership of the request. The worker
// releases the send claim itself when they say no, because nothing downstream
// will: the operation never became one.
bool handle_send(const Event& event)
{
    attadipa::core::MeshPeer peer{};
    for (std::size_t i = 0; i < service.peer_count(); ++i) {
        if (service.peer(i, peer) &&
            std::memcmp(peer.id.public_key.data(), event.peer_prefix.data(),
                        event.peer_prefix.size()) == 0) {
            const std::size_t length =
                static_cast<std::size_t>(std::find(event.text.begin(),
                                                   event.text.end(), '\0') -
                                         event.text.begin());
            if (service.send_private(peer.id,
                                     std::string_view(event.text.data(), length),
                                     event.timestamp)) {
                return true;
            }
            // The provider refused: the link is not ready, or a send it has not
            // finished is still in flight. Either way this request is over.
            ESP_LOGW(kTag, "the provider refused the send; it is not in flight");
            provider.send_abandoned();
            return false;
        }
    }
    ESP_LOGW(kTag, "requested contact prefix is not in retained chat contacts");
    provider.send_abandoned();
    return false;
}

bool handle_send_room(Event& event)
{
    const std::size_t text_length = static_cast<std::size_t>(
        std::find(event.text.begin(), event.text.end(), '\0') - event.text.begin());
    const bool accepted = provider.send_room(
        event.room, std::string_view(event.password.data(), event.password_length),
        std::string_view(event.text.data(), text_length), event.timestamp);
    std::fill(event.password.begin(), event.password.end(), '\0');
    if (!accepted) {
        ESP_LOGW(kTag, "Room Server message rejected by provider");
        provider.send_abandoned();
    }
    return accepted;
}

// Defined below handle_frame, which is its only caller: the identity settles
// on a frame, so it reads as part of the frame path rather than ahead of it.
void settle_node_identity(std::uint32_t generation);

void handle_frame(const Event& event)
{
    if (!session_owns(event.generation)) {
        // It was captured on a connection that has since ended. Feeding it to
        // the provider now would mix a dead session's bytes into a live one's
        // handshake, so it is dropped and counted with the rest.
        SessionGuard guard;
        owner.frame_dropped();
        return;
    }
    log_frame("RX", event.bytes.data(), event.size);
    const auto before = provider.status().delivery;
    const bool had_id = provider.status().has_node_id;
    if (provider.receive(event.bytes.data(), event.size, now()) &&
        provider.status().delivery != before) {
        ESP_LOGI(kTag, "MeshCore delivery %s",
                 attadipa::core::to_string(provider.status().delivery));
    }
    if (!had_id && provider.status().has_node_id) {
        settle_node_identity(event.generation);
    }
}

// The production instantiation of `forget_node()`; the other one is in
// tests/test_provisioning.cpp, and the sequence is in the header both compile.
// Runs on the worker, which owns `provider` and is the only task that may
// touch the bond store.
struct RealForgetOps {
    void disarm() { reconnect_allowed.store(false); }
    void terminate()
    {
        // As Deconfigure does: a scan already under way could still connect,
        // and with the pin half cleared that connection would adopt.
        if (ble_gap_disc_active()) (void)ble_gap_disc_cancel();
        const std::uint16_t connection = session_snapshot().connection;
        if (connection == attadipa::link::kNoSessionHandle) {
            // No session to end, so the disconnect path will not run
            // provider.begin() for us; Deconfigure makes the same call here.
            provider.begin(now());
            return;
        }
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
    bool take_forget(attadipa::firmware::BondIdentity& out)
    {
        SessionGuard guard;
        return recovery.take_forget(out);
    }
    bool delete_bond(const attadipa::firmware::BondIdentity& peer)
    {
        ble_addr_t address{};
        address.type = peer.type;
        std::memcpy(address.val, peer.address.data(), peer.address.size());
        const int rc = ble_store_util_delete_peer(&address);
        if (rc != 0) {
            ESP_LOGE(kTag,
                     "forget-node: the store refused to delete the bond for "
                     "xx:xx:xx:%02X:%02X:%02X (rc=%d); nothing changed",
                     static_cast<unsigned>(peer.address[2]),
                     static_cast<unsigned>(peer.address[1]),
                     static_cast<unsigned>(peer.address[0]), rc);
        }
        return rc == 0;
    }
    void record(const attadipa::firmware::BondIdentity& peer)
    {
        SessionGuard guard;
        recovery.record(peer);
    }
    bool erase_pin() { return erase_node_pin(); }
    bool unpin() { return provider.unpin(); }
    void clear_refusal()
    {
        refused_addr.store(0);
        refused_until_ms.store(0);
        hold_all_until_ms.store(0);
    }
};

const char* forget_node_name(attadipa::firmware::ForgetNodeOutcome outcome)
{
    using attadipa::firmware::ForgetNodeOutcome;
    switch (outcome) {
    case ForgetNodeOutcome::Forgotten:  return "bond deleted and pin cleared";
    case ForgetNodeOutcome::Unpinned:   return "pin cleared; no stale bond was recorded, the bond is kept";
    case ForgetNodeOutcome::BondKept:   return "the store refused; nothing changed";
    case ForgetNodeOutcome::PinOnFlash: return "pin cleared in memory only; the NVS erase refused";
    case ForgetNodeOutcome::Nothing:    return "nothing to forget";
    case ForgetNodeOutcome::Idle:
    case ForgetNodeOutcome::InFlight:   break;
    }
    return "?";
}

// The production instantiation of `settle_node_pin()`. The other one is in
// tests/test_session_owner.cpp, and there is one rule between them.
struct RealPinOps {
    // The session the identifying frame arrived on. Only the refuse path asks
    // about it; see the header for why adoption does not.
    std::uint32_t generation;

    bool node_id(attadipa::core::MeshPeerId& out) const
    {
        return provider.node_id(out);
    }
    bool pinned(attadipa::core::MeshPeerId& out) const
    {
        return provider.pinned(out);
    }
    bool wrong_node() const { return provider.wrong_node(); }
    bool store(const attadipa::core::MeshPeerId& id) { return store_node_pin(id); }
    void adopt(const attadipa::core::MeshPeerId& id) { provider.pin(id); }

    // Liveness, handle and peer address in ONE critical section. Read
    // separately, the address could be the *replacement's*: the wrong node
    // drops right after its SELF_INFO, the pinned node connects, and a refusal
    // that read `connecting_addr` a moment later would cool down the pinned
    // node and terminate its connection. `disconnect_fault()` above takes the
    // same three under the same guard for the same reason.
    attadipa::firmware::PinnedSession session() const
    {
        attadipa::firmware::PinnedSession live{};
        SessionGuard guard;
        live.live = owner.live(generation);
        if (live.live) {
            const std::uint16_t connection = owner.connection();
            live.has_connection = connection != attadipa::link::kNoSessionHandle;
            live.connection = connection;
            live.peer_addr = connecting_addr.load();
        }
        return live;
    }

    // Outside the guard, because `session_lock`'s rule is that nothing under it
    // calls into NimBLE.
    void cool_down(std::uint64_t addr)
    {
        const attadipa::firmware::RefusalState next =
            attadipa::firmware::after_refusal(refusal_state(), addr,
                                              uptime_ms());
        refused_addr.store(next.addr);
        refused_until_ms.store(next.addr_until_ms);
        hold_all_until_ms.store(next.hold_all_until_ms);
    }
    void disconnect(std::uint16_t connection)
    {
        // The whole refusal used to be this one call with its result thrown
        // away, so a terminate that did not take left the watch attached to a
        // node it will not talk to and said nothing. The link is harmless now
        // -- `receive()` answers a refused node nothing -- but it still holds
        // the radio, and that has to be visible somewhere.
        const int rc = ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGE(kTag, "could not disconnect the refused node: %d", rc);
        }
    }
};

// Called once per session, on the frame that first carries an identity. Every
// outcome says which node it is on, because "it changed and nothing said so" is
// the whole of #304. The decision itself is in meshcore_node_pin.h so that a
// host test can reach it; what is here is the logging and the real Ops.
void settle_node_identity(std::uint32_t generation)
{
    RealPinOps ops{generation};
    attadipa::core::MeshPeerId node{};
    attadipa::core::MeshPeerId expected{};
    const attadipa::firmware::PinOutcome outcome =
        attadipa::firmware::settle_node_pin(ops, node, expected);

    char seen[9];
    node_key_prefix(node, seen);
    char want[9];
    node_key_prefix(expected, want);

    switch (outcome) {
    case attadipa::firmware::PinOutcome::NoIdentity:
        break;
    case attadipa::firmware::PinOutcome::Adopted:
        // What makes every later session comparable to something.
        ESP_LOGW(kTag, "MeshCore node %s adopted as this watch's node", seen);
        break;
    case attadipa::firmware::PinOutcome::AdoptFailed:
        // Logged and not retried, so the next session adopts again rather than
        // the watch pretending it has a pin it could not store.
        ESP_LOGE(kTag,
                 "MeshCore node %s could not be stored; this watch stays "
                 "unpinned and will attach to whichever node answers first",
                 seen);
        break;
    case attadipa::firmware::PinOutcome::Pinned:
        ESP_LOGI(kTag, "MeshCore node %s, the pinned one", seen);
        break;
    case attadipa::firmware::PinOutcome::Refused:
        // Identified and then answered nothing: `MeshCoreCompanion::receive()`
        // drops every frame once `wrong_node()` has latched, so no contact
        // sync, no CMD_SYNC_NEXT_MESSAGE and no send ever reaches this node.
        //
        // WHAT IT CANNOT UNDO IS THE BOND, and round 1 of this change claimed
        // it could. The key arrives only in RESP_CODE_SELF_INFO, which is after
        // ENC_CHANGE -- so wherever a passkey is armed, the watch has already
        // paired and bonded with this node before anything here can know it is
        // the wrong one. Armed is a condition, not a given: it is
        // `firmware/main/meshcore_ble.cpp:167` -- "std::atomic_bool secure_pairing{false};",
        // stored from the operator's passkey at
        // `firmware/main/meshcore_ble.cpp:1452` -- "secure_pairing.store(event.passkey",
        // and it is what selects the SMP path at
        // `firmware/main/meshcore_ble.cpp:840` -- "if (secure_pairing.load()) {".
        // An image nobody has given a passkey to never gets this far. The store
        // holds one bond (`firmware/sdkconfig.defaults:116` --
        // "CONFIG_BT_NIMBLE_MAX_BONDS=1"), and on overflow NimBLE evicts rather
        // than refuses: the `ble_store_util_status_rr` installed in
        // `configure_host()` below answers BLE_STORE_EVENT_OVERFLOW for
        // OUR_SEC, PEER_SEC and PEER_ADDR with `ble_gap_unpair_oldest_peer()`
        // (`host/src/ble_store_util.c:453`), which unpairs
        // `oldest_peer_id_addr[0]` (`host/src/ble_gap.c:9516`) -- with one slot,
        // the pinned node's.
        //
        // So a wrong node in range costs the pinned node its bond, the refusal
        // is too late to prevent it, and the cooldown only bounds how often it
        // recurs. Preventing it needs a second bond slot or a way to know the
        // node before encrypting; both are provisioning, and #356's. What this
        // rule does buy is that the watch no longer *talks* to the wrong node,
        // which is what #304 measured.
        ESP_LOGW(kTag,
                 "MeshCore node %s is not this watch's node %s; disconnecting",
                 seen, want);
        break;
    case attadipa::firmware::PinOutcome::SessionOver:
        ESP_LOGD(kTag,
                 "MeshCore node %s is not this watch's node %s, and the "
                 "connection it arrived on is already over",
                 seen, want);
        break;
    }
}

void mesh_task(void*)
{
    Event event{};
    SessionMark applied{};
    // Worker-local, like `applied`: the request task claims the slot, but only
    // this task may release it, and only after it has seen the provider take
    // the operation. A claim released on any earlier pass would let a second
    // request in while the first was still sitting in the queue.
    bool send_owned = false;
    for (;;) {
        const bool received =
            xQueueReceive(event_queue, &event, kPollTicks) == pdTRUE;
        const SessionCatchUp catch_up = apply_lifecycle(applied);
        if (received) {
            switch (event.kind) {
            case EventKind::Wake:
                // Nothing to do. apply_lifecycle() above is what this event
                // exists to bring forward.
                break;
            case EventKind::Configure: {
                secure_pairing.store(event.passkey != 0);
                if (event.passkey != 0 &&
                    ble_sm_configure_static_passkey(event.passkey, true) != 0) {
                    // Whoever asked hears this. Before #416 the only record
                    // that the stack had refused was the fault the provider
                    // takes below, which nothing above the transport reads --
                    // so a watch whose passkey was never armed had already
                    // told its holder it was set up.
                    passkey_op.complete(
                        event.ticket,
                        attadipa::firmware::PasskeyOutcome::Refused);
                    provider.fault(now());
                    break;
                }
                // Stored after the stack has taken it and before the session
                // is told, so that what flash holds is a passkey this image
                // accepted. A refused write is this boot's problem only: the
                // watch is configured, and says it will not be next time --
                // and now says it to the screen as well, because "it will be
                // gone at the next boot" is not a fact a serial log can carry
                // to somebody holding the watch.
                const bool stored =
                    !event.persist_passkey || store_passkey(event.passkey);
                if (!stored) {
                    ESP_LOGE(kTag,
                             "MeshCore passkey armed but not stored; it will "
                             "not survive a power cycle");
                }
                configured.store(true);
                reconnect_allowed.store(true);
                // Armed and, where it had to be, on flash: everything the
                // request asked for has happened. What is left below is the
                // transport's own reconnection, which is not what the person
                // typing a passkey is waiting to hear.
                passkey_op.complete(
                    event.ticket,
                    stored ? attadipa::firmware::PasskeyOutcome::Armed
                           : attadipa::firmware::PasskeyOutcome::NotStored);
                // A Configure that lands on a live session is a
                // reconfiguration, and it cannot be applied to the session it
                // would reconfigure: the passkey above governs pairing, and
                // this connection has already paired under the previous one.
                //
                // provider.begin() used to run here unconditionally. Over a
                // live generation that put the transport in a state neither
                // machine allows on its own -- SessionOwner still Ready,
                // provider back to Attached -- so no second Ready transition
                // was ever replayed, provider.connected() was never called
                // again, no CMD_APP_START went out, and every arriving frame
                // was counted malformed until something disconnected. #345.
                //
                // Ending the session is enough, and it is all that is needed:
                // the disconnect path allocates a new generation, and
                // apply_lifecycle()'s Disconnected step is already the one
                // place that calls provider.begin() when a reconnect will be
                // attempted. The new session then reaches Ready through the
                // ordinary route, which is what re-sends CMD_APP_START.
                const std::uint16_t connection = session_snapshot().connection;
                if (connection != attadipa::link::kNoSessionHandle) {
                    (void)ble_gap_terminate(connection,
                                            BLE_ERR_REM_USER_CONN_TERM);
                    break;
                }
                provider.begin(now());
                if (session_snapshot().stack_readies != 0) start_scan();
                break;
            }
            case EventKind::Deconfigure: {
                configured.store(false);
                reconnect_allowed.store(false);
                if (!erase_passkey()) {
                    ESP_LOGE(kTag,
                             "MeshCore passkey not erased; the watch will "
                             "scan again at the next boot");
                }
                if (ble_gap_disc_active()) (void)ble_gap_disc_cancel();
                const std::uint16_t connection = session_snapshot().connection;
                if (connection != attadipa::link::kNoSessionHandle) {
                    (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
                } else {
                    provider.begin(now());
                }
                break;
            }
            case EventKind::Frame:
                handle_frame(event);
                break;
            case EventKind::OversizeFrame:
                provider.drop_oversize_frame();
                ESP_LOGW(kTag, "MeshCore frame over %u bytes dropped (%" PRIu32
                               " malformed so far); session kept",
                         static_cast<unsigned>(attadipa::link::kMeshCoreFrameBytes),
                         provider.malformed_frames());
                break;
            case EventKind::ForgetBond: {
                attadipa::firmware::BondIdentity peer{};
                bool taken = false;
                {
                    SessionGuard guard;
                    taken = recovery.take_forget(peer);
                }
                if (!taken) {
                    // Nothing conflicted, so there is no bond this operation
                    // is allowed to touch. The request task already refused on
                    // the same question; this is the second half of the same
                    // check, because the record can be cleared between them --
                    // and it answers with the same sentence, which is why the
                    // outcome is `Nothing` rather than the `Refused` that means
                    // a store said no.
                    ESP_LOGW(kTag, "forget-bond: no conflicting bond recorded");
                    forget_op.complete(attadipa::firmware::ForgetOutcome::Nothing);
                    break;
                }
                // End the link first. NimBLE does not defend a bond being
                // deleted underneath a live encrypted session, and the peer on
                // the other end of this one is the peer whose keys are gone.
                const std::uint16_t connection = session_snapshot().connection;
                if (connection != attadipa::link::kNoSessionHandle) {
                    (void)ble_gap_terminate(connection,
                                            BLE_ERR_REM_USER_CONN_TERM);
                }
                ble_addr_t address{};
                address.type = peer.type;
                std::memcpy(address.val, peer.address.data(),
                            peer.address.size());
                const int deleted = ble_store_util_delete_peer(&address);
                if (deleted != 0) {
                    // The store refused. Nothing has been sent to the host
                    // yet -- since #378 the answer waits for this line -- so
                    // it is told the failure rather than a success it would
                    // have to be talked out of. Running the command again is
                    // still the fix, and it can only be run again if the
                    // record goes back. Nothing is re-armed: the bond is still
                    // there, so the reconnect this would enable would fail on
                    // the same conflict.
                    ESP_LOGE(kTag,
                             "forget-bond: the store refused to delete the bond"
                             " for xx:xx:xx:%02X:%02X:%02X (rc=%d); it is still"
                             " there -- run mesh-forget-bond again",
                             static_cast<unsigned>(peer.address[2]),
                             static_cast<unsigned>(peer.address[1]),
                             static_cast<unsigned>(peer.address[0]), deleted);
                    {
                        SessionGuard guard;
                        recovery.record(peer);
                    }
                    forget_op.complete(attadipa::firmware::ForgetOutcome::Refused);
                    break;
                }
                ESP_LOGW(kTag,
                         "forget-bond: deleted the bond for xx:xx:xx:%02X:%02X:%02X;"
                         " pairing afresh on the next connection",
                         static_cast<unsigned>(peer.address[2]),
                         static_cast<unsigned>(peer.address[1]),
                         static_cast<unsigned>(peer.address[0]));
                // Exactly one attempt is armed, and it is armed by the owner:
                // the conflict record is consumed above, so a second
                // forget-bond with no new conflict is refused. This mirrors the
                // successful arm of EventKind::Configure -- reconnect was
                // disabled by disconnect_fault when the conflict was recorded.
                reconnect_allowed.store(true);
                provider.begin(now());
                if (session_snapshot().stack_readies != 0) start_scan();
                // Last, and only here: the bond is gone and the store said so.
                // This is the one path that may become a terminal MeshOk.
                forget_op.complete(attadipa::firmware::ForgetOutcome::Deleted);
                break;
            }
            case EventKind::ForgetNode: {
                RealForgetOps ops;
                const attadipa::firmware::ForgetNodeOutcome outcome =
                    attadipa::firmware::forget_node(ops);
                ESP_LOGW(kTag, "forget-node: %s", forget_node_name(outcome));
                // NOTHING IS RE-ARMED HERE, unlike ForgetBond above. The
                // watch is unpinned now, and a reconnect would adopt the
                // first node to answer before the person had typed the
                // passkey; the Configure that entry posts is the one arm.
                // The publish() at the foot of this loop is what takes
                // `has_pinned` off the status, so the request gate below
                // and the entry screen see the pin gone on the next read.
                forget_node_op.complete(event.ticket, outcome);
                break;
            }
            case EventKind::Send:
                if (handle_send(event)) send_owned = true;
                else send_claimed.store(false);
                break;
            case EventKind::SendRoom:
                if (handle_send_room(event)) send_owned = true;
                else send_claimed.store(false);
                break;
            }
        }
        // Every pass, not only an idle one. A node that posts anything at all
        // more often than kPollTicks -- adverts, LOG_RX_DATA, or a frame this
        // build counts as malformed -- keeps the branch above taken, and a
        // tick() that ran only in the `else` would never expire the ack budget
        // while that lasted. The node's output is a peer's output
        // (MESHCORE_PARSER_BOUNDS.md §5), so that input is not privileged.
        // It costs nothing: liveness is disabled on this link, so link_.tick()
        // has no work.
        provider.tick(now());
        // A terminal outcome -- confirmed, an explicit error, the ack budget
        // running out, or the session resetting -- releases the slot for the
        // next request. Read once per pass rather than signalled, for the same
        // reason the lifecycle is: a signal can be missed, a state cannot.
        if (send_owned && !provider.send_busy()) {
            send_owned = false;
            send_claimed.store(false);
        }
        if (catch_up.write_completed) {
            handle_write_result(catch_up.write_result, catch_up.write_generation);
        }
        // One read, used by both. A published MTU and the connection a frame is
        // written to therefore describe the same session or neither.
        const SessionSnapshot session = session_snapshot();
        publish(session);
        pump_tx(session);
    }
}

void on_reset(int reason)
{
    {
        // The host reset took every connection with it, and NimBLE delivers no
        // disconnect for one -- `reset_cb` *is* the notification. A session
        // left `Ready` here is a link model holding a handle that names
        // nothing. Ending it first also decides where the fault below lands:
        // raised after the session is over, it is replayed after the disconnect
        // and survives the reconnect that step performs, which is right,
        // because the stack stays down until `on_sync` says otherwise.
        SessionGuard guard;
        owner.ended();
    }
    stack_fault("NimBLE reset", reason);
}

void on_sync()
{
    std::uint8_t address_type = BLE_OWN_ADDR_RANDOM;
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &address_type) != 0) {
        stack_fault("BLE address configuration failed", BLE_HS_EAPP);
        return;
    }
    own_address_type.store(address_type);
    {
        SessionGuard guard;
        owner.stack_ready();
    }
    wake_worker();
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

extern "C" void ble_store_config_init(void);

namespace {

// The real steps behind meshcore_boot.h. Each is the one ESP-IDF call it names,
// so the order that header holds is the order this build executes.
struct RealBootOps {
    esp_err_t port_status = ESP_OK;

    bool port_init()
    {
        port_status = nimble_port_init();
        return port_status == ESP_OK;
    }
    void configure_host()
    {
        ble_hs_cfg.reset_cb = on_reset;
        ble_hs_cfg.sync_cb = on_sync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_mitm = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_store_config_init();
    }
    bool queue_create()
    {
        event_queue = xQueueCreate(kEventDepth, sizeof(Event));
        return event_queue != nullptr;
    }
    bool worker_create()
    {
        return xTaskCreate(mesh_task, "meshcore", 6144, nullptr, 3, nullptr) ==
               pdPASS;
    }
    void host_start() { nimble_port_freertos_init(host_task); }
    void queue_delete()
    {
        vQueueDelete(event_queue);
        event_queue = nullptr;
    }
    void port_deinit() { (void)nimble_port_deinit(); }
};

// A watch provisioned before this boot configures itself the way the operator
// did, through the same event, once the worker exists to take it. Posting
// before the host stack is up is fine: the worker arms the passkey at once
// and the scan waits for the stack's sync, as it does for a Configure from
// the bridge.
//
// The rule -- only a pairing passkey is stored, a stored zero is refused, a
// replay is not re-stored -- is `meshcore_passkey.h`; this is its NVS and its
// queue.
struct PasskeyOps {
    // Zero unless somebody is waiting on the answer; see Event::ticket.
    std::uint32_t ticket = 0;

    attadipa::firmware::StoredPasskey load(std::uint32_t& out)
    {
        return load_passkey(out);
    }
    bool configure(std::uint32_t passkey, bool persist)
    {
        Event event{EventKind::Configure};
        event.passkey = passkey;
        event.persist_passkey = persist;
        event.ticket = ticket;
        return post(event);
    }
};

void restore_passkey()
{
    using attadipa::firmware::PasskeyRestore;
    PasskeyOps ops;
    switch (attadipa::firmware::restore_passkey(ops)) {
    case PasskeyRestore::Restored:
        ESP_LOGI(kTag, "MeshCore passkey restored from flash");
        break;
    case PasskeyRestore::NotQueued:
        ESP_LOGE(kTag, "MeshCore passkey restored but not applied: queue full");
        break;
    case PasskeyRestore::Absent:
        ESP_LOGI(kTag, "no MeshCore passkey stored; BLE stays unconfigured");
        break;
    case PasskeyRestore::Unreadable:
        ESP_LOGE(kTag, "MeshCore passkey could not be read; BLE stays unconfigured");
        break;
    case PasskeyRestore::Refused:
        ESP_LOGE(kTag, "MeshCore passkey on flash is not six digits; ignored");
        break;
    }
}

}  // namespace

esp_err_t start_meshcore_ble()
{
    // Before anything the worker could read, and before the worker exists.
    // This used to run after xTaskCreate, which made it a plain write racing a
    // publish() that takes snapshot_lock -- one field, two tasks, one of them
    // unsynchronised.
    snapshot.availability = attadipa::core::Availability::Unprovisioned;

    // NVS BEFORE THE PIN IS READ, because nothing else guarantees it has been
    // done. The only other call in the image is inside the UI --
    // `firmware/main/waveshare_board.cpp:245` --
    // "state.metadata_storage = nvs_flash_init();"
    // -- and `firmware/main/attadipa_main.cpp:307` logs a UI failure and starts
    // the mesh anyway, so on that path the pin would be read out of an
    // uninitialised partition.
    //
    // Calling it twice is safe, VERIFIED against the pinned toolchain: the
    // partition manager returns early for a partition it has already
    // initialised (ESP-IDF v5.5.5,
    // `components/nvs_flash/src/nvs_partition_manager.cpp:41-44` --
    // "mStorage = lookup_storage_from_name(partition_label);" then
    // "if (mStorage) { return ESP_OK; }").
    //
    // NO ERASE-AND-RETRY ON ESP_ERR_NVS_NO_FREE_PAGES HERE. That erase takes
    // the BLE bonds and the time metadata with the pin; it is one policy for
    // the whole image, decided in ADR-0014: this firmware never erases NVS on
    // its own; recovery is a factory reset a person performs. What this file
    // owes is to say which of the two failures happened instead of reporting
    // both as "unpinned".
    const esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGE(kTag,
                 "NVS unavailable (%s): this watch can neither read nor store a "
                 "MeshCore pin",
                 esp_err_to_name(nvs_err));
    }

    // Also before the worker exists, and for the same reason: `provider` is
    // worker-owned from the moment the task starts, and a pin written from
    // here afterwards would be a second writer.
    attadipa::core::MeshPeerId pinned{};
    switch (load_node_pin(pinned)) {
    case PinRead::Pinned: {
        provider.pin(pinned);
        char want[9];
        node_key_prefix(pinned, want);
        ESP_LOGI(kTag, "MeshCore node pinned to %s", want);
        break;
    }
    case PinRead::Unpinned:
        ESP_LOGI(kTag,
                 "no MeshCore node pinned; the first node that answers will be "
                 "adopted as this watch's node");
        break;
    case PinRead::Unreadable:
        // Left unpinned, and said so as its own line: this watch will adopt,
        // and the adoption will not stick either, because the same storage has
        // to take the write.
        ESP_LOGE(kTag,
                 "MeshCore pin could not be read; this watch stays unpinned and "
                 "will attach to whichever node answers first");
        break;
    }

    RealBootOps ops;
    switch (attadipa::firmware::boot_meshcore(ops)) {
    case attadipa::firmware::BootResult::Ok:
        restore_passkey();
        return ESP_OK;
    case attadipa::firmware::BootResult::PortInitFailed:
        // The bootstrap used to create the queue and start the worker *before*
        // this call, so a failure here left a task polling a queue nothing
        // would ever post to for the rest of the boot -- roughly 24 KiB held,
        // ESTIMATED -- while app_main logged that MeshCore had failed safely.
        return ops.port_status;
    case attadipa::firmware::BootResult::QueueFailed:
    case attadipa::firmware::BootResult::WorkerFailed:
        break;
    }
    return ESP_ERR_NO_MEM;
}

bool configure_meshcore_ble(std::uint32_t passkey)
{
    // Whether it outlives this boot is `meshcore_passkey.h`'s call: a pairing
    // passkey does, the unpaired probe's zero does not -- replaying that at
    // boot would leave a product image scanning in the clear with nobody
    // having asked, and the Room Server password crosses the air inside those
    // frames.
    //
    // No ticket: this is the debug channel's `mesh-configure`, which is
    // watching the console the worker logs to. The watch's own screen is not,
    // and takes meshcore_ble_configure_passkey() below instead.
    PasskeyOps ops;
    return attadipa::firmware::request_passkey(ops, passkey);
}

esp_err_t meshcore_ble_configure_passkey(std::uint32_t passkey,
                                         std::uint32_t& ticket)
{
    // `ticket` is written only on ESP_OK: on every refusal it keeps naming
    // whatever the caller last had in flight, which the busy refusal below
    // is precisely about -- the screen reads that ticket next to learn
    // whether the earlier passkey may still arm (#416, review round 2).
    // Only a pairing passkey. The unpaired probe is a diagnostic somebody asks
    // for while watching the console; a screen with a keypad cannot ask for it
    // and must not be able to arm an unencrypted scan by typing six zeros.
    if (!attadipa::firmware::is_pairing_passkey(passkey)) {
        return ESP_ERR_INVALID_ARG;
    }
    // Reserved before the event is queued, and one at a time -- the same order
    // meshcore_ble_forget_bond() uses, for the same reason: a slot claimed
    // after the post is a slot the worker can complete before it exists.
    std::uint32_t reserved = 0;
    if (!passkey_op.reserve(reserved)) return ESP_ERR_NOT_FINISHED;
    PasskeyOps ops;
    ops.ticket = reserved;
    if (!attadipa::firmware::request_passkey(ops, passkey)) {
        // SAID OUT LOUD, for the reason the forget-bond one is: `post()` is a
        // zero-wait `xQueueSend` and writes nothing of its own, so without
        // this line a request the watch threw away and a request that never
        // arrived leave the same evidence. The screen is told too -- this
        // returns a failure and the entry stays where it is -- but the log is
        // the only place that says which of the two ways it failed.
        ESP_LOGE(kTag,
                 "MeshCore passkey: the worker queue was full, so the request"
                 " was dropped before the stack saw it; the watch is not"
                 " configured");
        passkey_op.release(reserved);
        return ESP_ERR_NO_MEM;
    }
    ticket = reserved;
    return ESP_OK;
}

attadipa::firmware::PasskeyOutcome
meshcore_ble_passkey_outcome(std::uint32_t ticket)
{
    return passkey_op.take(ticket);
}

bool stop_meshcore_ble()
{
    // Disable in the caller before the host callback gets another chance to
    // react to an in-flight discovery or disconnect event.
    const bool was_configured = configured.exchange(false);
    const bool was_reconnecting = reconnect_allowed.exchange(false);
    if (post(Event{EventKind::Deconfigure})) return true;
    // The post is zero-wait, so a full queue refuses it -- and then nothing
    // will ever carry this transition: the session stays Ready, the connection
    // is never terminated, and the provider is never told. The caller is
    // already told the truth (`OperationFailed`), so the state must be the
    // truth too. Put both flags back rather than leave the transport
    // half-stopped: publish() would report Unprovisioned over a live session's
    // facts, which reads as a device that stopped when it did not.
    configured.store(was_configured);
    reconnect_allowed.store(was_reconnecting);
    return false;
}

bool meshcore_ble_send(const std::array<std::uint8_t, 6>& peer_prefix,
                       std::string_view text,
                       attadipa::core::WallTime timestamp)
{
    if (text.empty() || text.size() > attadipa::core::kMeshTextBytes) {
        return false;
    }
    if (!claim_send()) return false;
    Event event{EventKind::Send};
    event.peer_prefix = peer_prefix;
    event.timestamp = timestamp;
    std::memcpy(event.text.data(), text.data(), text.size());
    event.text[text.size()] = '\0';
    const bool posted = post(event);
    if (!posted) send_claimed.store(false);
    return posted;
}

bool meshcore_ble_send_room(
    const std::array<std::uint8_t, attadipa::core::kMeshPublicKeyBytes>& room,
    std::string_view password, std::string_view text,
    attadipa::core::WallTime timestamp)
{
    if (password.empty() || password.size() > 15 || text.empty() ||
        text.size() > attadipa::core::kMeshTextBytes) {
        return false;
    }
    if (!claim_send()) return false;
    Event event{EventKind::SendRoom};
    event.room = room;
    event.password_length = static_cast<std::uint8_t>(password.size());
    std::memcpy(event.password.data(), password.data(), password.size());
    event.timestamp = timestamp;
    std::memcpy(event.text.data(), text.data(), text.size());
    event.text[text.size()] = '\0';
    const bool accepted = post(event);
    std::fill(event.password.begin(), event.password.end(), '\0');
    if (!accepted) send_claimed.store(false);
    return accepted;
}

esp_err_t meshcore_ble_forget_bond()
{
    // Refused where the caller can still be told. The worker checks again
    // because it is the only task that may consume the record, but a refusal
    // discovered there could only be logged -- and "there is no bond to
    // forget" is the answer the owner actually needs.
    //
    // The two refusals are different answers and are kept apart: an empty
    // record is a statement about the request, a full queue is a statement
    // about the transport, and telling the operator the first when the second
    // happened sends them looking for a conflict that is recorded.
    {
        SessionGuard guard;
        if (!recovery.recovery_required()) return ESP_ERR_INVALID_STATE;
    }
    // Reserved before the event is queued, and one at a time. Two requests
    // arriving back to back used to be two `ESP_OK`s over one deletion; the
    // second is now refused here, where the caller can still be told, rather
    // than discovered by the worker with nowhere to say it. `ESP_OK` from here
    // no longer means the bond is gone -- it means the request was taken, and
    // meshcore_ble_forget_bond_outcome() is where the answer arrives.
    if (!forget_op.reserve()) return ESP_ERR_NOT_FINISHED;
    if (!post(Event{EventKind::ForgetBond})) {
        // SAID OUT LOUD, because the failure text this returns to the host
        // sends the operator to this log. `post()` is a zero-wait
        // `xQueueSend` and writes nothing of its own, so without this line a
        // request the watch threw away and a request that never arrived are
        // the same evidence -- on a recovery path where the log is the only
        // second signal there is.
        ESP_LOGE(kTag,
                 "forget-bond: the worker queue was full, so the request was"
                 " dropped before the bond store saw it; the bond is still"
                 " there -- run mesh-forget-bond again");
        forget_op.release();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

attadipa::firmware::ForgetOutcome meshcore_ble_forget_bond_outcome()
{
    return forget_op.take();
}

esp_err_t meshcore_ble_forget_node(std::uint32_t& ticket)
{
    // Something to forget, or the request is refused where the caller can
    // still be told: a recorded stale bond, or a pin. The pin is read from
    // the published status because `provider` is worker-owned; the worker
    // reads the real thing and answers `Nothing` if it changed in between.
    bool anything = false;
    {
        SessionGuard guard;
        anything = recovery.recovery_required();
    }
    if (!anything) anything = meshcore_ble_status().has_pinned;
    if (!anything) return ESP_ERR_INVALID_STATE;
    std::uint32_t reserved = 0;
    if (!forget_node_op.reserve(reserved)) return ESP_ERR_NOT_FINISHED;
    Event event{EventKind::ForgetNode};
    event.ticket = reserved;
    if (!post(event)) {
        ESP_LOGE(kTag, "forget-node: the worker queue was full; nothing changed");
        forget_node_op.release(reserved);
        return ESP_ERR_NO_MEM;
    }
    ticket = reserved;
    return ESP_OK;
}

attadipa::firmware::ForgetNodeOutcome
meshcore_ble_forget_node_outcome(std::uint32_t ticket)
{
    return forget_node_op.take(ticket);
}

attadipa::core::MeshStatus meshcore_ble_status()
{
    taskENTER_CRITICAL(&snapshot_lock);
    const attadipa::core::MeshStatus result = snapshot;
    taskEXIT_CRITICAL(&snapshot_lock);
    return result;
}
