# QMI8658 FIFO acquisition for paired AK raw capture

Scope: the opt-in RAM checkpoint under #450, following the measured AK-only
[archive](ak09911-waveshare-2026-09-09/README.md). This is not a heading provider.
The first [physical paired capture](qmi-paired-waveshare-2026-09-09/README.md)
produced 398 AK and 3810 QMI records, but QMI cleanup returned InvalidData.
Axis mapping and concurrent step operation remain
**NOT EXECUTED — HARDWARE REQUIRED**; this is not a clean cleanup checkpoint.

## Verified sources

QST **13-52-27, QMI8658C Rev A, 20 June 2022**, SHA-256
`9887d8827b1769dff31ad5d5c0c4672458d593af5309aa0ed183d38440f7defd`:
Table 22 (controls), Table 23 (FIFO units), sections 5.10 (command handshake),
6.2/6.3 (Non-SyncSample and DRDY), 8 (FIFO), 11 (pedometer).
The primary PDF is retained at `/home/hleserg/attadipa-bench/qmi8658c_revA.pdf`.

**VERIFIED:** section 11, printed page 64, restricts the pedometer to
Non-SyncSample. Preserving Pedo_EN alone while enabling SyncSample does not
preserve ongoing step detection. Section 6.3, printed page 48, requires direct
data-register reads within the DRDY high interval; arbitrary burst polling is
not a documented coherency guarantee.

The [exact Waveshare V1.0 schematic](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/blob/b099739ad0e33b34e5fbaae77f02bd84805d79a3/Schematic/ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf),
SHA-256 `6d531fb458863c666210c92294a07204d675bcb7997a54fc219d92fadbbacf9d`,
page 1, U5/6/9-Axis block: **VERIFIED** INT1 pin 4 connects to GPIO21;
INT2 pin 9 connects to TP15. An INT2-to-MCU GPIO connection is **UNKNOWN**.
FIFO status polling needs no additional wire or guessed DRDY GPIO.

[SensorLib at 2b9e591f](https://github.com/lewisxhe/SensorLib/blob/2b9e591f245e447d3d00ec8798c3f49b897882d9/src/sensor/imu/qmi8658/SensorQMI8658.cpp)
was inspected as a burst/layout example, not imported. Its read-error path
omits FIFO release, its successful release write is unchecked, and its command
helper can report success after never observing CmdDone. The new sequence
checks those transitions. The historical pedometer probe also cannot serve as
a read-only preflight: it unconditionally reconfigures and resets step count.

## Acquisition policy — ASSUMED until bench validation

The same `Qmi8658Fifo` sequence is used by the native probe and its host test.
One shared native register transport owns individual device handles; the probe
owns the bus. The paired option is disabled by default and restricted to RAM.

1. Read identity, controls, FIFO state and matching step-count snapshots before
   writes. Refuse a busy FIFO/command, SyncSample, disabled oscillator or
   unsupported active configuration. ID `05/7C` selects the supported map;
   it does not authenticate a C variant against an A variant.
2. Preserve an active accelerometer and any gyro, including step-engine bits,
   range and ODR. For a fully idle IMU only, temporarily enable the already
   exercised ±8 g / 125 Hz accel-only profile. An inactive step engine is
   explicitly reported, not presented as a continuous-step PASS.
3. Temporarily establish little-endian/address-increment and polled command
   handshake, preserving other control bits. Use a 16-sample FIFO; complete
   sample sets are six bytes or twelve when gyro is active. FIFO count is in
   **two-byte words**, while the watermark is in complete samples.
4. Request read mode with command `05`, observe CmdDone, acknowledge `00`,
   observe completion clear, read frozen count and complete payload, then verify
   read-mode release before publishing a batch. Partial failures never refresh
   the last good batch. Each command phase has a 20 ms software polling budget;
   this is not a hard native wall-clock bound because one I2C operation has its
   own 50 ms timeout.
5. On exit, resolve any pending command where possible, stop FIFO filling,
   record remaining words, reset only the owned FIFO with command `04`, and
   restore/read back the saved settings. Never issue step reset `0F`, toggle
   Pedo_EN, reset the chip, change rails or use AHB-gating commands. A pending
   command that cannot be resolved makes cleanup fail rather than look verified.

FIFO read mode pauses FIFO filling; incoming samples can be dropped (section
8.7). Full/overflow conditions and read-mode dwell therefore need bench checks.
Restoring control bytes cannot restore consumed samples or unreadable internal
history. The start/end step snapshots are diagnostics, not a claim that the
hardware's counted steps are accurate or that a multi-byte read is atomic.

## Records and checks

The paired loop runs for 40 seconds, bounded further by either sensor's fatal
acquisition error. `AKRAW` retains its existing format. `QMIBATCH` carries batch
number, host request/freeze/release timestamps, item count and FIFO status;
`QMIRAW` carries batch number, item index and raw sensor-axis acceleration XYZ.
No FIFO item's conversion time is equated with its host read-completion time.
Scale/ODR are derived from the logged control values during analysis.

After the first physical run exposed an ambiguous `stop=4`, `QMI stop_check`
records the first failing stop step, post-reset word count with validity, and
the first register readback mismatch with expected/actual values. These are
collected in RAM and printed after stop/close. No extra bus operations, waits,
or relaxed checks are added. A later readback mismatch can coexist with an
earlier failing step; their fields must not be assumed to name the same event.
The diagnostic image remains **NOT EXECUTED — HARDWARE REQUIRED**.

The `qmi8658_fifo` host test calls the production sequence with a transport model:
six/twelve-byte layout, signed values, idle/active cleanup, no-write admission,
initialization fault injection, incomplete payload/release, overflow, capacity
and command timeout. `ak09911` remains the reused acquisition regression.
The existing RAM CI build explicitly enables the paired path, checks its
generated configuration and runs both RAM ELF guards. None of these is a
physical compass, tilt, calibration or step-count PASS.

## Executed stop diagnostic, 2026-09-09 14:00 UTC

The [preserved second paired run](qmi-stop-waveshare-2026-09-09/README.md) identifies `count_after_reset` as the first failing step: three words remain after the command handshake, with no register readback mismatch. The temporary accelerometer is disabled later, not before that count check. The actual reason remains UNKNOWN; the next bounded observation records full FIFO_STATUS and a post-restoration count without suppressing the failed verdict. Ordinary firmware ELF `2915714b7` and saved brightness 5% were restored and verified.
