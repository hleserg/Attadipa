# Waveshare AK09911 raw acquisition evidence — 2026-09-09

**MEASURED:** the corrected raw probe read the soldered AK09911-compatible
module on the Waveshare. This directory preserves the source console and
normal-boot records, their original analysis and run metadata, and the actual
generated build configuration. The owner authorised repository publication on
2026-09-09. Imported measurement files are byte-identical to the original run;
`SHA256.json` covers this focused archive. Local Git attributes preserve original
line endings and console whitespace. The original files remain preserved.

The hardware fact and wiring index stays in
[MAGNETOMETER_RETROFIT](../MAGNETOMETER_RETROFIT.md), §2.1. The contemporaneous
[bench result in #450](https://github.com/hleserg/Attadipa/issues/450#issuecomment-5596759154)
records the handoff. This archive supplies the evidence behind that result.

## Inspect or recompute without hardware

From the repository root:

```sh
python3 docs/research/ak09911-waveshare-2026-09-09/verify.py
```

The standard-library script checks archive hashes and recomputes sample count,
timestamp intervals, XYZ statistics, status pairs and the logged cleanup result.
It also checks the archived normal-boot markers. It performs no device I/O and
is not a new hardware test. `ANALYSIS.json` is the original result, not output
silently regenerated on each verification.

| Original evidence | What it records |
| --- | --- |
| `console.txt` | Complete RAM console, including all 199 `AKRAW` records, identity and cleanup summary |
| `INPUT.json`, `build-sdkconfig.txt` | Source/image/loader identity, timing, USB unit, build configuration and original local paths |
| `ANALYSIS.json` | Original aggregates calculated from the raw records |
| `LOAD_RESULT.json` | Loader exit result and completion time |
| `NORMAL_BOOT.txt`, `NORMAL_BOOT_CONTROL.txt`, `NORMAL_BOOT_RESULT.json` | Explicit reset step, installed-firmware boot output and capture completion |

Each `AKRAW` record is `received_at_us,x,y,z,st1,st2`. Time is the host-side
sensor read-completion timestamp in the ESP32 image; it is not UTC or a sensor
sample-time measurement. XYZ are signed sensor-axis counts; status bytes are
hexadecimal. Mounting transforms and adjusted field units are absent.

## Tested source and reproduction boundary

The measured source is commit `3cd2b66ea7e13d9e2ad19e1d613cda7873ce2993`.
The 185536-byte RAM image had SHA-256
`85f70ac2c6e08f2251cf93278ac6128518dd519b16330d5fc1ec26cd651aa939`.
`INPUT.json` preserves individual acquisition-source hashes. The build preceded
that commit and retained a cached `1c15d7a9-dirty` description; that banner is not
the tested source identity.

The existing [RAM loader](../../../tools/flash/ramhold.py) has exactly the bytes
used in this capture: SHA-256
`809120281bb5207b0904219669b75a3d512253a9a6b389bf9436958d361ae36d`.
It is reused rather than copied into this archive. The image was built by the
RAM-variant step in [.github/workflows/ci.yml](../../../.github/workflows/ci.yml)
at the tested source commit, with a fresh generated configuration and both ELF
guards passing. The archived `build-sdkconfig.txt` (the original `sdkconfig`, unchanged) enables the I2C and AK probes and selects
Waveshare SDA15/SCL14. The runtime capture budget is 20 seconds; the loader held
the serial port for 40 seconds. An equivalent invocation after building that RAM
image and obtaining the bench device is:

```sh
python3 tools/flash/ramhold.py path/to/attadipa.bin 40 \
  --serial 28:84:85:B2:18:A4 --log path/to/new-console.txt
```

Use a new output path for a new run. The factory backup and board-specific
bring-up instructions remain the prerequisites in
[BRINGUP_2026-08-25](../../hardware/BRINGUP_2026-08-25.md).
The compiled BIN/ELF, verbose build output and vendor PDFs remain in the original
local evidence directory recorded by `INPUT.json`; they are not needed to
recompute the published raw observations. No flash or erase command was used in
this run. The subsequent normal boot is a separate observation, not proof that
normal firmware never writes NVS.

## What this run establishes

The log contains ID `48 05`, 199 accepted samples in 20.004042 seconds,
1802 not-ready polls, zero observed overflow/invalid/DOR, successful power-down
readback and `ESP_OK` device close. The normal-boot record reaches `UI ready`.
This is log evidence; it is not rendered-screen acceptance. The diagnostic ASA
bytes are `17 17 13` with fuse-mode readback `00`; their factory-adjustment
validity and silicon authenticity remain **UNKNOWN**.

Physical sensor pose and stationarity were not independently measured. The
recorded standard deviations are not a sensor noise floor or heading accuracy.
Fixed mounting, QMI-to-watch frame validation, iron calibration, tilt, heading,
wrist/case effects and vibration A/B are **NOT EXECUTED — HARDWARE REQUIRED**
for this run. Earlier QMI bench evidence remains in the existing facts index;
this AK capture does not replace or invalidate it. Motor installation follows a
working calibrated compass.
