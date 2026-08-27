#include "meshcore_ble.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstring>

#include "attadipa/link/meshcore_companion.h"
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

constexpr char kTag[] = "attadipa_mesh_ble";
constexpr std::size_t kEventDepth = 16;
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(500);
constexpr TickType_t kMeshCoreWriteDelay = pdMS_TO_TICKS(60);
constexpr std::uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;

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

enum class EventKind : std::uint8_t {
    StackReady,
    Configure,
    Deconfigure,
    PeerArriving,
    Ready,
    Disconnected,
    Fault,
    Frame,
    OversizeFrame,
    WriteDone,
    Send,
    SendRoom,
};

struct Event {
    EventKind kind = EventKind::Fault;
    std::uint16_t size = 0;
    std::int32_t result = 0;
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
portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
attadipa::core::MeshStatus snapshot{};
std::uint16_t connection = kNoConnection;
std::uint16_t service_start = 0;
std::uint16_t service_end = 0;
std::uint16_t rx_handle = 0;
std::uint16_t tx_handle = 0;
std::uint16_t cccd_handle = 0;
std::uint16_t negotiated_mtu = 0;
std::uint8_t own_address_type = BLE_OWN_ADDR_RANDOM;
bool stack_ready = false;
std::atomic_bool configured{false};
std::atomic_bool secure_pairing{false};
std::atomic_bool reconnect_allowed{false};
std::atomic_bool scan_report_seen{false};
bool gatt_ready = false;
bool write_in_flight = false;

attadipa::core::MonotonicTime now()
{
    return {static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
}

bool post(const Event& event)
{
    return event_queue != nullptr && xQueueSend(event_queue, &event, 0) == pdTRUE;
}

void publish()
{
    attadipa::core::MeshStatus next = provider.status();
    next.mtu = negotiated_mtu;
    if (!configured.load()) {
        next.availability = attadipa::core::Availability::Unprovisioned;
    }
    taskENTER_CRITICAL(&snapshot_lock);
    snapshot = next;
    taskEXIT_CRITICAL(&snapshot_lock);
}

void clear_gatt()
{
    connection = kNoConnection;
    service_start = 0;
    service_end = 0;
    rx_handle = 0;
    tx_handle = 0;
    cccd_handle = 0;
    negotiated_mtu = 0;
    gatt_ready = false;
    write_in_flight = false;
}

int gap_event(ble_gap_event* event, void*);

void start_scan()
{
    if (!stack_ready || !configured.load() || !reconnect_allowed.load() ||
        ble_gap_disc_active()) {
        return;
    }
    ble_gap_disc_params params{};
    params.passive = 0;
    // An active scan may receive the MeshCore name or service UUID only in the
    // scan response. Do not discard it after the preceding advertisement.
    params.filter_duplicates = 0;
    const int rc = ble_gap_disc(own_address_type, BLE_HS_FOREVER, &params,
                                gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "start scan failed: %d", rc);
        (void)post(Event{EventKind::Fault});
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

void disconnect_fault(const char* step, int rc)
{
    ESP_LOGE(kTag, "%s failed: %d", step, rc);
    reconnect_allowed.store(false);
    (void)post(Event{EventKind::Fault});
    if (connection != kNoConnection) {
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}

int subscribe_done(std::uint16_t, const ble_gatt_error* error,
                   ble_gatt_attr*, void*)
{
    if (error->status != 0) {
        disconnect_fault("subscribe", error->status);
    } else {
        gatt_ready = true;
        ESP_LOGI(kTag, "Companion GATT ready, MTU %u",
                 static_cast<unsigned>(negotiated_mtu));
        (void)post(Event{EventKind::Ready});
    }
    return 0;
}

int descriptor_discovered(std::uint16_t conn, const ble_gatt_error* error,
                          std::uint16_t, const ble_gatt_dsc* descriptor,
                          void*)
{
    if (error->status == 0 && descriptor != nullptr) {
        if (ble_uuid_cmp(&descriptor->uuid.u, kCccdUuid) == 0) {
            cccd_handle = descriptor->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE || cccd_handle == 0) {
        disconnect_fault("discover CCCD", error->status);
        return 0;
    }
    const std::uint8_t enable[] = {1, 0};
    const int rc = ble_gattc_write_flat(conn, cccd_handle, enable,
                                        sizeof(enable), subscribe_done, nullptr);
    if (rc != 0) disconnect_fault("write CCCD", rc);
    return 0;
}

int tx_discovered(std::uint16_t conn, const ble_gatt_error* error,
                  const ble_gatt_chr* characteristic, void*)
{
    if (error->status == 0 && characteristic != nullptr) {
        tx_handle = characteristic->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || tx_handle == 0) {
        disconnect_fault("discover TX characteristic", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_all_dscs(conn, tx_handle,
                                            service_end, descriptor_discovered,
                                            nullptr);
    if (rc != 0) disconnect_fault("start CCCD discovery", rc);
    return 0;
}

int rx_discovered(std::uint16_t conn, const ble_gatt_error* error,
                  const ble_gatt_chr* characteristic, void*)
{
    if (error->status == 0 && characteristic != nullptr) {
        rx_handle = characteristic->val_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || rx_handle == 0) {
        disconnect_fault("discover RX characteristic", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_chrs_by_uuid(conn, service_start,
                                                service_end, kTxUuid,
                                                tx_discovered, nullptr);
    if (rc != 0) disconnect_fault("start TX discovery", rc);
    return 0;
}

int service_discovered(std::uint16_t conn, const ble_gatt_error* error,
                       const ble_gatt_svc* discovered, void*)
{
    if (error->status == 0 && discovered != nullptr) {
        service_start = discovered->start_handle;
        service_end = discovered->end_handle;
        return 0;
    }
    if (error->status != BLE_HS_EDONE || service_start == 0) {
        disconnect_fault("discover Companion service", error->status);
        return 0;
    }
    const int rc = ble_gattc_disc_chrs_by_uuid(conn, service_start,
                                                service_end, kRxUuid,
                                                rx_discovered, nullptr);
    if (rc != 0) disconnect_fault("start RX discovery", rc);
    return 0;
}

void discover_service(std::uint16_t conn)
{
    const int rc = ble_gattc_disc_svc_by_uuid(conn, kServiceUuid,
                                               service_discovered, nullptr);
    if (rc != 0) disconnect_fault("start service discovery", rc);
}

int mtu_done(std::uint16_t conn, const ble_gatt_error* error,
             std::uint16_t mtu, void*)
{
    if (error->status != 0) {
        disconnect_fault("MTU exchange", error->status);
    } else {
        negotiated_mtu = mtu;
        discover_service(conn);
    }
    return 0;
}

int write_done(std::uint16_t, const ble_gatt_error* error,
               ble_gatt_attr*, void*)
{
    Event event{EventKind::WriteDone};
    event.result = error->status;
    if (!post(event)) disconnect_fault("queue write result", BLE_HS_ENOMEM);
    return 0;
}

int gap_event(ble_gap_event* event, void*)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        if (!scan_report_seen.exchange(true)) {
            ESP_LOGI(kTag, "received BLE advertising report");
        }
        if (!configured.load() || !reconnect_allowed.load()) return 0;
        if (!advertises_meshcore(event->disc)) return 0;
        ESP_LOGI(kTag, "matched MeshCore advertisement");
        if (ble_gap_disc_cancel() != 0) return 0;
        (void)post(Event{EventKind::PeerArriving});
        if (ble_gap_connect(own_address_type, &event->disc.addr, 30000,
                            nullptr, gap_event, nullptr) != 0) {
            start_scan();
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            start_scan();
            return 0;
        }
        connection = event->connect.conn_handle;
        if (!configured.load()) {
            ESP_LOGI(kTag, "closing connection after local stop");
            (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        if (secure_pairing.load()) {
            ESP_LOGI(kTag, "MeshCore connected; starting BLE security");
            if (ble_gap_security_initiate(connection) != 0) {
                disconnect_fault("start pairing", BLE_HS_EAUTHEN);
            }
        } else if (ble_gattc_exchange_mtu(connection, mtu_done, nullptr) != 0) {
            disconnect_fault("start MTU exchange", BLE_HS_EAPP);
        }
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0) {
            disconnect_fault("pairing", event->enc_change.status);
            return 0;
        }
        if (ble_gattc_exchange_mtu(event->enc_change.conn_handle,
                                   mtu_done, nullptr) != 0) {
            disconnect_fault("start MTU exchange", BLE_HS_EAPP);
        }
        return 0;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        // A notification queued during the previous connection can be delivered
        // before this connection has rediscovered and subscribed to TX.
        if (event->notify_rx.conn_handle != connection || !gatt_ready ||
            event->notify_rx.attr_handle != tx_handle) {
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
        incoming.size = length;
        if (os_mbuf_copydata(event->notify_rx.om, 0, length,
                             incoming.bytes.data()) != 0 || !post(incoming)) {
            disconnect_fault("queue notification", BLE_HS_ENOMEM);
        }
        return 0;
    }
    case BLE_GAP_EVENT_MTU:
        negotiated_mtu = event->mtu.value;
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(kTag, "MeshCore disconnected: %d", event->disconnect.reason);
        clear_gatt();
        (void)post(Event{EventKind::Disconnected});
        start_scan();
        return 0;
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
void log_frame(const char* direction, const std::uint8_t* data, std::size_t size)
{
    if (data == nullptr || size == 0) return;
    ESP_LOGI(kTag, "%s op=0x%02X len=%u", direction,
             static_cast<unsigned>(data[0]), static_cast<unsigned>(size));
    ESP_LOG_BUFFER_HEX_LEVEL(kTag, data, size, ESP_LOG_INFO);
}

void pump_tx()
{
    if (!gatt_ready || write_in_flight || connection == kNoConnection) return;
    attadipa::link::MeshCoreFrame frame{};
    if (!provider.next_tx(frame)) return;
    const std::uint16_t payload_limit = negotiated_mtu > 3
                                            ? negotiated_mtu - 3
                                            : 0;
    if (frame.size > payload_limit) {
        disconnect_fault("frame exceeds negotiated ATT payload", BLE_HS_EMSGSIZE);
        return;
    }
    const int rc = ble_gattc_write_flat(connection, rx_handle,
                                         frame.bytes.data(), frame.size,
                                         write_done, nullptr);
    if (rc != 0) {
        disconnect_fault("write Companion frame", rc);
    } else {
        write_in_flight = true;
        log_frame("TX", frame.bytes.data(), frame.size);
    }
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

void mesh_task(void*)
{
    Event event{};
    for (;;) {
        if (xQueueReceive(event_queue, &event, kPollTicks) == pdTRUE) {
            switch (event.kind) {
            case EventKind::StackReady:
                stack_ready = true;
                provider.begin(now());
                if (configured.load()) start_scan();
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
                    if (stack_ready) start_scan();
                }
                break;
            case EventKind::Deconfigure:
                configured.store(false);
                reconnect_allowed.store(false);
                if (ble_gap_disc_active()) (void)ble_gap_disc_cancel();
                if (connection != kNoConnection) {
                    (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
                } else {
                    provider.begin(now());
                }
                break;
            case EventKind::PeerArriving:
                provider.peer_arriving(now());
                break;
            case EventKind::Ready:
                provider.connected(now());
                break;
            case EventKind::Disconnected:
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
            case EventKind::Fault:
                provider.fault(now());
                break;
            case EventKind::Frame:
                {
                    log_frame("RX", event.bytes.data(), event.size);
                    const auto before = provider.status().delivery;
                    if (provider.receive(event.bytes.data(), event.size, now()) &&
                        provider.status().delivery != before) {
                        ESP_LOGI(kTag, "MeshCore delivery %s",
                                 attadipa::core::to_string(provider.status().delivery));
                    }
                }
                break;
            case EventKind::OversizeFrame:
                provider.drop_oversize_frame();
                ESP_LOGW(kTag, "MeshCore frame over %u bytes dropped (%" PRIu32
                               " malformed so far); session kept",
                         static_cast<unsigned>(attadipa::link::kMeshCoreFrameBytes),
                         provider.malformed_frames());
                break;
            case EventKind::WriteDone:
                write_in_flight = false;
                if (event.result != 0) {
                    // A write that failed is the peer not answering, not a
                    // broken subsystem, and calling fault() here is what
                    // wedged the session on the bench. ATT allows a
                    // write-with-response 30 s before the host must tear the
                    // link down, so this callback arrives immediately before
                    // BLE_GAP_EVENT_DISCONNECT -- and Faulted refuses
                    // PeerArriving and PeerEstablished alike, so the
                    // reconnect that follows could never re-establish the
                    // session. Terminating routes the failure into the
                    // disconnect path below, which is the single place that
                    // recovers; when the link is already gone that path is
                    // running anyway.
                    ESP_LOGW(kTag, "Companion write failed: %d; recycling the session",
                             event.result);
                    if (connection != kNoConnection) {
                        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
                    }
                } else {
                    vTaskDelay(kMeshCoreWriteDelay);
                }
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
        publish();
        pump_tx();
    }
}

void on_reset(int reason)
{
    ESP_LOGE(kTag, "NimBLE reset: %d", reason);
    (void)post(Event{EventKind::Fault});
}

void on_sync()
{
    if (ble_hs_util_ensure_addr(0) != 0 ||
        ble_hs_id_infer_auto(0, &own_address_type) != 0) {
        (void)post(Event{EventKind::Fault});
        return;
    }
    (void)post(Event{EventKind::StackReady});
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
