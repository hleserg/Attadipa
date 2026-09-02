# 0014 — Time source, trust and synchronization

Status: **accepted**
Date: 2026-08-26

## Context

A valid calendar read from an RTC proves only that the register contents can be
decoded. It does not prove when the RTC was set, whether it is UTC or local
time, whether it has drifted, or whether the current timezone offset is still
correct. The watch also needs to accept time later from a companion, a network,
GNSS or mesh without exposing any of those transports to Clock.

Freshness cannot be derived from wall time: correcting the wall clock would
then also change the deadline used to decide whether that correction is fresh.
Across a reboot there is no continuous monotonic timestamp, so persisted trust
cannot honestly resume where it left off.

## Decision

- `TimeService` is the sole application-facing time owner. It carries UTC and
  derives local presentation time; Clock never reads a chip or transport.
- Every observation states its source, quality, monotonic observation time,
  source age and freshness lifetime. Source does not imply quality. A
  decodable RTC calendar is `Provisional` and therefore `Stale`, never silently
  `Valid`.
- A fresh trusted observation wins over a lower-quality one. Among equally
  trusted fresh observations the order is GNSS, network, companion, mesh,
  manual, RTC, simulated. An expired observation may be replaced by a lower
  source rather than leaving no time at all.
- Freshness and correction-warning windows use monotonic time. A correction
  larger than five minutes between fresh trusted observations requires an
  explicit override.
- RTC registers contain UTC. The numeric local offset is separate and has its
  own freshness deadline. The offset is persisted so local presentation stays
  numerically correct across reboot, but it is restored as provisional: the UI
  warns that time may be outdated until a source synchronizes again.
- The first real input is the existing physical USB debug connection:
  `watch_control.py sync-time`. It sends host UTC, the host's
  current offset and a bounded lifetime. The device commits the offset
  metadata to NVS first, then writes PCF85063 seconds through years in one
  transaction, reads the calendar back, and only then acknowledges (#264
  reversed the order: a metadata layer that cannot be written is found before
  the chip is touched). The host does not retry a lost acknowledgement because
  that could repeat a wall-clock write.

  **That is the bench path, and has been only that since #346.**
  `firmware/sdkconfig.defaults:89` — "CONFIG_ATTADIPA_WATCH_CONTROL=n", so the
  opcode above exists only in the HIL build. The product's first real input is
  the watch itself: a long press on the clock opens an entry screen for the
  date, the time, the offset and the node passkey
  (`apps/include/attadipa/apps/provisioning.h:55` — "class ProvisioningEntry {"); a Cancel key
  leaves it at any point, and nothing reaches the board before OK on the
  offset (`apps/include/attadipa/apps/provisioning.h:35` — "Cancel,"), so a
  long press made by accident costs one key and not a retyped clock. A board
  that failed that write may have moved the chip (the RTC is written last, and
  nothing puts it back), so a Cancel after it keeps the failure on screen rather
  than saying nothing changed. The board's
  `firmware/main/waveshare_board.cpp:445` — "set_wall_clock(const attadipa::core::WallClockEntry &entry) override {"
  runs the same `provision_time()` sequence as the opcode, with the same
  order and the same one-day lifetime. Whoever holds the watch may set it —
  [ADR-0018](0018-owner-consent-for-provisioning.md) and OD-26 decided that.
- This slice stores an effective offset, not an IANA zone database or a DST
  rule. A future companion or network provider refreshes the offset before its
  deadline. If it does not, local time becomes stale rather than silently
  asserting that an old seasonal offset is current.
- Default NVS is initialised once per boot and its verdict kept
  (`firmware/main/waveshare_board.cpp:269` — "state.metadata_storage = nvs_flash_init();").
  A verdict other than success is logged once and every
  synchronization of that boot fails before the RTC is touched, with the same
  `Failed` (the host's `OperationFailed`) as a store that cannot be read: the
  device is at fault, not the request, and the boot log carries the reason. This firmware never calls `nvs_flash_erase()` on its
  own, whatever the verdict — `ESP_ERR_NVS_NO_FREE_PAGES` and
  `ESP_ERR_NVS_NEW_VERSION_FOUND` are the two ESP-IDF answers with "erase the
  partition and try again" — because default NVS is also the BLE bond store
  and the MeshCore pin. That erase is a factory reset a person performs with
  the factory image backed up first (`idf.py erase-flash`, then reflash); until
  then the clock runs degraded and keeps showing. The BLE side initialises the
  same partition for its own reasons and applies the same policy.

The single-transaction RTC rule follows NXP's
[PCF85063A datasheet Rev. 7.3](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf),
§7.4: seconds through years are one access and the access completes within one
second.

## Alternatives considered

- **Trust any syntactically valid RTC value.** Rejected: stale factory or
  manually written data would look authoritative forever.
- **Persist the trusted deadline across reboot.** Rejected: the monotonic clock
  restarts, while using corrected wall time to reconstruct freshness creates a
  circular trust claim.
- **Put timezone/DST conversion in Clock.** Rejected: every consumer would
  acquire its own source and expiry policy.
- **Ship a full timezone database now.** Rejected: the measured vertical slice
  needs one current offset; zone-rule distribution belongs with the future
  companion/network provider.

## Consequences

The displayed local time survives ordinary reset, but a reset deliberately
brings back the stale warning. A successful new synchronization removes it.
Companion, network, GNSS and mesh adapters can later submit observations to the
same service without changing Clock or the RTC boundary. Long-term RTC drift,
automatic source adapters and timezone-rule delivery remain separate measured
work rather than claims made by this decision.
