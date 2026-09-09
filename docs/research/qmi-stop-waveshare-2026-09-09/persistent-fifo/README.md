# Persistent nonempty FIFO — 2026-09-09

MEASURED on Waveshare `28:84:85:B2:18:A4`, source `35512fc1bddaa31bebe6edb51571615ef2d614f4`. RAM binary 189952 bytes, SHA-256 `3f695cb4fa890b528c611efe26564e436c84cfc9edeab0d07ba1e22e3f3be616`. The image ran 14:13:21–14:14:22 UTC; ordinary restoration finished 14:14:31 UTC. Console SHA-256 `7107ead9e486b2e362c60f81f1e3ce71d32bbb0494416d83db9424aa5988099e`.

QMI again refused entry with Busy, before any QMI register write or sample acquisition. After its unowned no-op stop, the four read-only observations were:

```text
QMI stop_final valid=1 count_lo=03 status=50 command_valid=1 command=00 command_status=00
```

The count is three words. Under QST 13-52-27 Rev A Table 23 (printed page 34), status 0x50 asserts FIFO_NOT_EMPTY and FIFO_WTM; FIFO_FULL/FIFO_OVERFLOW and the count's high bits are zero. Bit 4 means NOT EMPTY, not EMPTY. Section 8.4 (page 53) defines the count as pending filled content, not a cumulative total since the last request. CTRL9 and STATUSINT both read zero, so no outstanding command or CmdDone is observed. This identifies the nonempty FIFO as the measured admission blocker after ordinary MCU reboots. It does not prove the contents of the three words or the cause of the earlier failed reset.

The complete initial snapshot has CTRL1=20 and the other seven saved controls zero, including FIFO bypass, disabled accelerometer/gyroscope and motion engines; steps=0. The start ownership guard remains intact. `stop=0` is an unowned no-op, not a cleanup PASS. Zero sample counts and the incomplete final driver snapshot are expected for refused ownership; no second 40-second acquisition occurred.

The eight-second restored boot log shows ordinary ELF `2915714b7`, no USB watch-control endpoint, UI ready at saved 5%, and alive through six seconds without panic. The port was closed; fuser reported no holder. A normal MCU boot is therefore not proof of a reset sensor/FIFO. These are boot observations, not UI appearance acceptance.

## Boundary and next decision

The reset failure remains unresolved. The prior post-reset count was read while the temporary accelerometer was still enabled; this later observation finds disabled sensors and persistent reported contents. It does not distinguish an ineffective reset from a last in-flight sample or other sensor behavior. No guessed delay, FIFO takeover, step reset or power-cycle workaround was added.

The next finite hypothesis test is coordinated with the architect: recover the explicitly identified bench residue, then compare stopping the owned temporary accelerometer before FIFO clearing with the current sequence, or evaluate the documented FIFO request/drain route. Neither is yet a verified fix. Preserve active-pedometer settings and the Busy entry guard. Calibration, tilt compensation, heading accuracy and vibration effects remain outside these measurements.

SHA256.json identifies the five original files. INPUT.json records the earlier build freeze. The paired-stream verifier intentionally rejects this no-acquisition archive; do not interpret that as corrupt evidence or weaken its two-stream requirement.
