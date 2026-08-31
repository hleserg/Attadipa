#include "meshcore_ble.h"
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
};

struct Event {
    EventKind kind = EventKind::Wake;
    std::uint32_t generation = 0;
    std::uint16_t size = 0;
    std::uint32_t passkey = 0;
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

int gap_event(ble_gap_event* event, void* arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        if (!scan_report_seen.exchange(true)) {
            ESP_LOGI(kTag, "received BLE advertising report");
        }
        if (!configured.load() || !reconnect_allowed.load()) return 0;
        if (!advertises_meshcore(event->disc)) return 0;
        ESP_LOGI(kTag, "matched MeshCore advertisement");
        if (ble_gap_disc_cancel() != 0) return 0;
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
            disconnect_fault(generation, "pairing", event->enc_change.status);
            return 0;
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
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
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

void handle_send(const Event& event)
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
            (void)service.send_private(peer.id,
                                       std::string_view(event.text.data(), length),
                                       event.timestamp);
            return;
        }
    }
    ESP_LOGW(kTag, "requested contact prefix is not in retained chat contacts");
}

void handle_send_room(Event& event)
{
    const std::size_t text_length = static_cast<std::size_t>(
        std::find(event.text.begin(), event.text.end(), '\0') - event.text.begin());
    const bool accepted = provider.send_room(
        event.room, std::string_view(event.password.data(), event.password_length),
        std::string_view(event.text.data(), text_length), event.timestamp);
    std::fill(event.password.begin(), event.password.end(), '\0');
    if (!accepted) ESP_LOGW(kTag, "Room Server message rejected by provider");
}

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
    if (provider.receive(event.bytes.data(), event.size, now()) &&
        provider.status().delivery != before) {
        ESP_LOGI(kTag, "MeshCore delivery %s",
                 attadipa::core::to_string(provider.status().delivery));
    }
}

void mesh_task(void*)
{
    Event event{};
    SessionMark applied{};
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
            case EventKind::Configure:
                secure_pairing.store(event.passkey != 0);
                if (event.passkey != 0 &&
                    ble_sm_configure_static_passkey(event.passkey, true) != 0) {
                    provider.fault(now());
                } else {
                    configured.store(true);
                    reconnect_allowed.store(true);
                    provider.begin(now());
                    if (session_snapshot().stack_readies != 0) start_scan();
                }
                break;
            case EventKind::Deconfigure: {
                configured.store(false);
                reconnect_allowed.store(false);
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
            case EventKind::Send:
                handle_send(event);
                break;
            case EventKind::SendRoom:
                handle_send_room(event);
                break;
            }
        } else {
            provider.tick(now());
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

esp_err_t start_meshcore_ble()
{
    event_queue = xQueueCreate(kEventDepth, sizeof(Event));
    if (event_queue == nullptr) return ESP_ERR_NO_MEM;
    if (xTaskCreate(mesh_task, "meshcore", 6144, nullptr, 3, nullptr) != pdPASS) {
        vQueueDelete(event_queue);
        event_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t result = nimble_port_init();
    if (result != ESP_OK) return result;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_store_config_init();
    snapshot.availability = attadipa::core::Availability::Unprovisioned;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

bool configure_meshcore_ble(std::uint32_t passkey)
{
    if (passkey > 999999) return false;
    Event event{EventKind::Configure};
    event.passkey = passkey;
    return post(event);
}

bool stop_meshcore_ble()
{
    // Disable in the caller before the host callback gets another chance to
    // react to an in-flight discovery or disconnect event.
    configured.store(false);
    reconnect_allowed.store(false);
    return post(Event{EventKind::Deconfigure});
}

bool meshcore_ble_send(const std::array<std::uint8_t, 6>& peer_prefix,
                       std::string_view text,
                       attadipa::core::WallTime timestamp)
{
    if (text.empty() || text.size() > attadipa::core::kMeshTextBytes) {
        return false;
    }
    Event event{EventKind::Send};
    event.peer_prefix = peer_prefix;
    event.timestamp = timestamp;
    std::memcpy(event.text.data(), text.data(), text.size());
    event.text[text.size()] = '\0';
    return post(event);
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
    Event event{EventKind::SendRoom};
    event.room = room;
    event.password_length = static_cast<std::uint8_t>(password.size());
    std::memcpy(event.password.data(), password.data(), password.size());
    event.timestamp = timestamp;
    std::memcpy(event.text.data(), text.data(), text.size());
    event.text[text.size()] = '\0';
    const bool accepted = post(event);
    std::fill(event.password.begin(), event.password.end(), '\0');
    return accepted;
}

attadipa::core::MeshStatus meshcore_ble_status()
{
    taskENTER_CRITICAL(&snapshot_lock);
    const attadipa::core::MeshStatus result = snapshot;
    taskEXIT_CRITICAL(&snapshot_lock);
    return result;
}
