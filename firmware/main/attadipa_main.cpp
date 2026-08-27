// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// The device entry point, and for now the whole of the firmware.
//
// Its job is the device composition root: report the board, then start the
// Waveshare hardware slice. Board-specific drivers stay beside this file and
// never leak into core/ or apps/.
//
// Every number printed below is read from the silicon at run time. None of it is
// a constant copied out of the research notes, because the point of the first
// transcript off a real board is to *check* those notes, and a diagnostic that
// prints what it was told cannot disagree with anybody.

#include <cinttypes>
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

#include "attadipa/core/diagnostics.h"
#include "attadipa/l10n/tr.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/platform/hardware_feature.h"

#include "waveshare_board.h"
#if CONFIG_BT_NIMBLE_ENABLED
#include "meshcore_ble.h"
#endif

namespace {

constexpr char kTag[] = "attadipa";

// Which board this build believes it is on. It is a build-time answer today
// because there is exactly one board on the desk; when the T-Watch arrives this
// becomes a Kconfig choice rather than a constant, and the profile lookup below
// is already the seam for it.
constexpr char kBoardProfileId[] = "waveshare-amoled-206";

// ESP-IDF's reset reason is a device fact; core::ResetReason is the vocabulary
// the rest of Attadipa reports in. Translating here rather than passing the
// ESP-IDF enum upwards is the same rule as everywhere else: nothing above the
// board layer learns which SDK produced the answer.
//
// The two ESP-IDF reasons that have no core:: counterpart are deliberate. A
// JTAG-driven reset and an unspecified USB reset are host actions, not device
// events, and mapping them onto ExternalPin would report a developer's cable as
// a hardware fault in a field log.
attadipa::core::ResetReason translate_reset_reason(esp_reset_reason_t reason)
{
    using attadipa::core::ResetReason;
    switch (reason) {
    case ESP_RST_POWERON:  return ResetReason::PowerOn;
    case ESP_RST_SW:       return ResetReason::Software;
    case ESP_RST_PANIC:    return ResetReason::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return ResetReason::Watchdog;
    case ESP_RST_BROWNOUT: return ResetReason::Brownout;
    case ESP_RST_DEEPSLEEP:return ResetReason::DeepSleepWake;
    case ESP_RST_EXT:      return ResetReason::ExternalPin;
    default:               return ResetReason::Unknown;
    }
}

// Free heap in the two pools that fail independently. `largest free block` is
// the number that matters and `free` is the one that reassures: a heap with
// 200 KB free in 4 KB fragments cannot serve a framebuffer, and only the first
// number says so.
void fill_memory(attadipa::core::MemoryStatus& memory)
{
    memory.heap_free_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    memory.heap_largest_block_bytes =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    memory.heap_minimum_ever_bytes =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

#if CONFIG_SPIRAM
    // Left unset rather than zeroed when PSRAM did not come up. `0 bytes free`
    // and `no PSRAM` are different findings and a snapshot that cannot tell them
    // apart is the failure diagnostics.h §1 is about.
    if (esp_psram_is_initialized()) {
        memory.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        memory.psram_largest_block_bytes =
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    }
#endif
}

void report_silicon()
{
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    // ESP-IDF packs the revision as major * 100 + minor. v0.2 is 2 — and this
    // build requires REV_MIN 0 because no later die exists and all eight errata
    // in sheet v1.3 apply. If this ever prints something other than v0.2, the
    // errata document is describing a different part than the one in hand.
    ESP_LOGI(kTag, "SoC        : %s rev v%d.%d, %d core(s), %s%s",
             CONFIG_IDF_TARGET,
             chip.revision / 100, chip.revision % 100,
             chip.cores,
             (chip.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip.features & CHIP_FEATURE_BLE) ? "BLE" : "");

    // Read the JEDEC id off the part rather than trusting the build config.
    // GD25Q256EYIGR answers 0xC8 0x4019 — GigaDevice, 2^25 bytes = 32 MB —
    // and the difference from the 16 MB this image declares is the point:
    // only the low half is addressable, so the mismatch is expected and the
    // bootloader's warning about it is a reminder rather than a fault.
    // A pure-RAM image has no flash driver at all — `APP_BUILD_TYPE_PURE_RAM_APP`
    // leaves Cache, MMU, Flash and PSRAM uninitialised and drops the components
    // that speak to them, so `CONFIG_ESPTOOLPY_FLASHSIZE` is not merely 0 here,
    // it does not exist and the reference is a compile error. The route that
    // writes nothing to the part is also the route that cannot ask the part
    // anything, and this is where that shows.
#if CONFIG_APP_BUILD_TYPE_PURE_RAM_APP
    ESP_LOGW(kTag, "Flash      : not initialised — this is a pure-RAM image, "
                   "and the JEDEC id needs the flash driver");
#else
    std::uint32_t jedec = 0;
    esp_err_t     err   = esp_flash_read_id(nullptr, &jedec);
    if (err == ESP_OK) {
        // The low byte of a JEDEC id is log2 of the capacity in bytes: 0x19 is
        // 2^25 = 32 MB. Deriving the size from it rather than asking the flash
        // driver is deliberate — the driver reports the size this *build*
        // configured, which is 16 MB here on purpose, and the whole point of
        // this line is to show the physical part disagreeing with it.
        const std::uint8_t capacity_code = jedec & 0xFF;
        char               physical[16]  = "unrecognised";
        // 0x14 and not 0x10: the code is log2 of the size in *bytes*, so
        // anything below 20 is a part smaller than a megabyte and would shift
        // by a negative amount, which is undefined rather than merely wrong.
        if (capacity_code >= 0x14 && capacity_code <= 0x1A) {
            std::snprintf(physical, sizeof(physical), "%u MB",
                          1u << (capacity_code - 20));
        }
        // CONFIG_ESPTOOLPY_FLASHSIZE is a string ("16MB"); the per-size
        // CONFIG_..._16MB symbols are *absent* rather than 0 when unselected,
        // so testing one in an expression is a compile error rather than a
        // false branch. Kconfig booleans belong in the preprocessor.
        ESP_LOGI(kTag, "Flash      : JEDEC %02" PRIx32 " %02" PRIx32 " %02" PRIx32
                       ", %s on the part, %s declared by this build",
                 (jedec >> 16) & 0xFF, (jedec >> 8) & 0xFF, jedec & 0xFF,
                 physical, CONFIG_ESPTOOLPY_FLASHSIZE);
    } else {
        ESP_LOGE(kTag, "Flash      : JEDEC read failed (%s)", esp_err_to_name(err));
    }
#endif

#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        // The mode comes from the build, the size from the chip. If an octal
        // build reports 2 MB here, this is not the R8 the eFuses described.
#if CONFIG_SPIRAM_MODE_OCT
        constexpr const char* kMode = "octal";
#else
        constexpr const char* kMode = "quad";
#endif
        ESP_LOGI(kTag, "PSRAM      : %u MB, %s SPI, initialised",
                 static_cast<unsigned>(esp_psram_get_size() / (1024u * 1024u)),
                 kMode);
    } else {
        ESP_LOGE(kTag, "PSRAM      : configured but NOT initialised — "
                       "the build says octal 8 MB and the chip did not answer");
    }
#else
    ESP_LOGW(kTag, "PSRAM      : disabled in this build");
#endif
}

// Log the partition table the bootloader accepted. The build declares 16 MB, so
// ESP-IDF rejects an on-device table that crosses that size before app_main;
// tools/flash/partition_check.py independently checks the repository CSV.
//
// A PURE_RAM_APP image has no partition table to read, and that is not a fault.
void report_partitions()
{
#if CONFIG_APP_BUILD_TYPE_PURE_RAM_APP
    // Measured, and it cost a panic to learn: `esp_partition_find()` reaches
    // `spi_flash_mmap`, which in a pure-RAM image asserts rather than returning
    // nothing — `assert failed: spi_flash_mmap spi_flash_mmap.c:200`, because
    // the mmap callbacks are only registered when the flash driver is built in.
    // The first run of this firmware on the board printed every line above this
    // one and then rebooted here. "No partition table to read" and "no flash
    // driver to ask" are different states, and only one of them is survivable.
    ESP_LOGW(kTag, "Partitions : not readable — a pure-RAM image has no flash "
                   "driver, and asking anyway is a panic rather than an empty "
                   "answer");
    return;
#else
    esp_partition_iterator_t it =
        esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    if (it == nullptr) {
        ESP_LOGW(kTag, "Partitions : none readable "
                       "(expected for a RAM-loaded image)");
        return;
    }

    for (; it != nullptr; it = esp_partition_next(it)) {
        const esp_partition_t* part = esp_partition_get(it);
        ESP_LOGI(kTag, "Partition  : %-8s type %u/%-2u  0x%06" PRIx32
                       " + 0x%06" PRIx32,
                 part->label, part->type, part->subtype, part->address, part->size);
    }
    esp_partition_iterator_release(it);
#endif
}

// The proof that platform/ links and answers. It is a lookup rather than a
// hardcoded string so that the failure mode — a profile id that no longer
// exists — is loud here instead of silent later.
void report_board_profile()
{
    const attadipa::platform::BoardProfile* profile =
        attadipa::platform::find_board_profile(kBoardProfileId);
    if (profile == nullptr) {
        ESP_LOGE(kTag, "Board      : no profile '%s' in this build", kBoardProfileId);
        return;
    }

    ESP_LOGI(kTag, "Board      : %s (%s), %ux%u %s, %u dpi",
             profile->name, profile->id,
             static_cast<unsigned>(profile->display.width_px),
             static_cast<unsigned>(profile->display.height_px),
             profile->display.technology == attadipa::platform::PanelTechnology::Amoled
                 ? "AMOLED" : "IPS",
             static_cast<unsigned>(profile->display.dpi()));
}

}  // namespace

extern "C" void app_main(void)
{
    const esp_app_desc_t* app = esp_app_get_description();

    ESP_LOGI(kTag, "--- %s boot -------------------------------------------",
             attadipa::l10n::tr(attadipa::l10n::StringId::ProductName));
    ESP_LOGI(kTag, "Build      : %s %s, ESP-IDF %s", app->version, app->date,
             esp_get_idf_version());

    // Snapshot first, printing second. The struct is core's vocabulary and the
    // firmware fills it exactly as the simulator does — which is the point of
    // filling it at all in a skeleton with nothing to display it on. When the
    // debug channel reaches the device (T-114), this is already the shape it
    // ships.
    attadipa::core::DiagnosticsSnapshot snapshot{};
    snapshot.build.version = app->version;
    // Left null rather than filled with something adjacent. ESP-IDF's app
    // description has no separate commit field, and copying `version` into both
    // would make a snapshot claim a provenance it does not have. Stamping a
    // real hash is a build-system change and belongs to whoever needs it.
    snapshot.build.commit   = nullptr;
    snapshot.build.built_at = app->date;
    snapshot.uptime.ms      = static_cast<std::uint64_t>(esp_timer_get_time() / 1000);
    snapshot.reset_reason   = translate_reset_reason(esp_reset_reason());
    fill_memory(snapshot.memory);

    ESP_LOGI(kTag, "Reset      : %s (ESP-IDF code %d)",
             attadipa::core::to_string(snapshot.reset_reason),
             static_cast<int>(esp_reset_reason()));

    report_silicon();
    report_board_profile();
    report_partitions();

    ESP_LOGI(kTag, "Heap       : %" PRIu32 " free, %" PRIu32 " largest block, "
                   "%" PRIu32 " lowest ever (internal)",
             snapshot.memory.heap_free_bytes.value_or(0),
             snapshot.memory.heap_largest_block_bytes.value_or(0),
             snapshot.memory.heap_minimum_ever_bytes.value_or(0));
    if (snapshot.memory.psram_free_bytes.has_value()) {
        ESP_LOGI(kTag, "PSRAM heap : %" PRIu32 " free, %" PRIu32 " largest block",
                 *snapshot.memory.psram_free_bytes,
                 *snapshot.memory.psram_largest_block_bytes);
    }
    ESP_LOGI(kTag, "-------------------------------------------------------------");

#if !CONFIG_APP_BUILD_TYPE_PURE_RAM_APP
    const esp_err_t ui_err = start_waveshare_ui();
    if (ui_err != ESP_OK) {
        ESP_LOGE(kTag, "Waveshare UI failed safely: %s", esp_err_to_name(ui_err));
    }
#if CONFIG_BT_NIMBLE_ENABLED
    const esp_err_t mesh_err = start_meshcore_ble();
    if (mesh_err != ESP_OK) {
        ESP_LOGE(kTag, "MeshCore BLE failed safely: %s",
                 esp_err_to_name(mesh_err));
    } else {
        // The link model is now the transport's own, and it moves at runtime.
        // This is the boot value, not a standing claim.
        ESP_LOGI(kTag, "Link model : %s (MeshCore companion over BLE)",
                 attadipa::core::to_string(meshcore_ble_status().transport));
    }
#endif
#else
    ESP_LOGI(kTag, "Waveshare UI skipped in PURE_RAM mode");
#endif

    // A heartbeat rather than a return. app_main returning is legal and deletes
    // its task, which leaves a board that has booted looking exactly like a
    // board that has hung — no output either way. One line a second is the
    // cheapest possible answer to "is it still alive", and it is what the
    // hardware procedure in docs/hardware/ tells a reader to look for.
    std::uint32_t tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(kTag, "alive %" PRIu32 "s, heap %" PRIu32, ++tick,
                 static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
}
