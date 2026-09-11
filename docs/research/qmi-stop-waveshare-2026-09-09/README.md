# QMI stop diagnostic — 2026-09-09

MEASURED on Waveshare USB serial `28:84:85:B2:18:A4`, source `2dcb8eccf669217b8f0956f86e5e715bca5147e6`. The frozen RAM binary is 189376 bytes, SHA-256 `058f72be718ad33354182f673dabb291521babcb5b85b62d1d8f4207c8e4ee9d`. Capture began 13:58:50 UTC; ordinary boot restoration finished 14:00:00 UTC. Console SHA-256: `5b891438b79c2c6078e0e044ebc20e2cf9b2922a9e5e05404aa124b8fae1bc1a`.

## Result: failed check identified, cleanup still FAIL

The diagnostic recorded `step=count_after_reset remaining_valid=1 remaining_words=3 mismatch_valid=0`. The first failed check is the count read after the FIFO reset command and its completed command/acknowledgement handshake. No stop register readback mismatch was recorded. `stop=4` remains InvalidData; matching final controls do not override it.

The same run delivered 398 AK09911 samples in 40.042592 seconds, with overflow/invalid/DOR all zero, and 3824 QMI accelerometer samples in 3115 batches. Both device closes returned ESP_OK; AK power-down returned 0. The temporary accelerometer-only path ran; all eight final controls matched the initial idle snapshot and steps stayed zero. An active pedometer and the 12-byte gyro/accelerometer path were not exercised.

The accelerometer was still enabled at the failed count check. The driver disables its temporary accelerometer later during restoration. The final disabled snapshot does not prove it was disabled during reset. QST 13-52-27 Rev A section 5.10.6.2 (printed page 43) and section 8.10 (page 54) require FIFO reset to clear the count; section 5.10.1 (page 41) defines the completed-command handshake. Section 8.8 (page 53) reads count before REQ_FIFO. These sections do not document a count latch requiring REQ_FIFO, a post-ACK delay, a sensors-off prerequisite, or a bypass-mode exception. The physical reason for the residual count remains UNKNOWN.

Next diagnostic: preserve full FIFO_STATUS from the existing post-reset read, then observe count/status once after restoration. Two new single-byte reads occur after stop, outside its command sequence; their validity is explicit and they do not replace the failed stop result. This is an observation, not a proposed fix or proof of any of the hypotheses above.

The [follow-up attempt](busy-entry/README.md) executed but returned Busy before acquisition; it therefore did not measure the post-restoration count. A further read-only observation includes CTRL9/STATUSINT and runs after stop even on a refused entry with a complete initial snapshot. Its four reads do not change start/stop commands or verdicts. Ordinary boot alone is not evidence of an admissible QMI entry state.

That [read-only observation](persistent-fifo/README.md) is now executed: count=3, FIFO_STATUS=50 (NOT_EMPTY and WTM), CTRL9=00, STATUSINT=00. The persistent nonempty FIFO is the measured entry blocker. Why the prior reset failed remains UNKNOWN; no cleanup or heading PASS is claimed.

## The candidate this document owed #488, written down 2026-09-11

Still UNKNOWN, and now with one named candidate instead of none. The [drain session](../ak09911-drain-waveshare-2026-09-11/README.md) measured that a CTRL9 `REQ_FIFO` (`05`) issued with `FIFO_CTRL` in bypass moves nothing on this part: the command completed, the payload read returned `0x8000` words and the count did not change. `stop()` issues its FIFO reset from exactly that state — `firmware/main/qmi8658_fifo.h:226` — "keep(set(0x14, 0), " — puts the part in bypass, and `:231` — "keep(command(0x04), " — is the command whose count check then fails, the one the comment there marks as "distinct from step reset 0x0f".

**This is a candidate, not a finding, and the distinction is the whole point of writing it here.** `04` and `05` are different commands and the sections cited above say nothing about a bypass exception for either. Nothing measured a `04` from bypass against a `04` from a FIFO mode on this board, and until something does, reordering the stop path would be a guessed fix — the failure mode this whole document exists to avoid. What changed on 2026-09-11 is that the next person choosing a diagnostic for #488 has a hypothesis with a measurement behind its neighbour, rather than the four the datasheet already fails to support.

## Ordinary boot and limits

The architect's actual handoff is [issue #450 comment 5602884117](https://github.com/hleserg/Attadipa/issues/450#issuecomment-5602884117). After this RAM run, the eight-second raw boot log again shows ELF `2915714b7`, the ordinary image without the USB watch-control endpoint, UI ready at saved brightness 5%, and alive through 6 seconds without panic. The port was closed and fuser reported no opener. No flash-write command was used for this diagnostic. This is boot evidence, not visual acceptance or a claim that normal firmware never writes NVS.

Pose, reference heading, mounting rigidity, calibration, tilt compensation and direction accuracy remain UNKNOWN or NOT EXECUTED. Host timestamps are transaction boundaries, not conversion times. ASA validity remains UNKNOWN (`fuse_mode=00`); AK values remain raw counts. Earlier demonstrated magnetometer motion response is not retracted. Motor testing remains after a working calibrated compass.

## Reproduce

Run `python3 docs/research/qmi-paired-waveshare-2026-09-09/verify.py docs/research/qmi-stop-waveshare-2026-09-09`. It verifies five original file hashes and stream structure and regenerates ANALYSIS.json. Its zero exit means structural checks passed; `qmi_cleanup_check` remains FAIL. INPUT.json preserves the original pre-run freeze state; this report and console supply the later executed result. The identified binary, ELF and configuration remain in the private frozen bench directory. No raw archive byte was normalized.
