# First physical paired AK09911 / QMI8658 capture — 2026-09-09

Source: `3ac23007abc058f30189975cf18d52f3dc6d16f3` (#515). Binary: 188720 bytes, SHA-256 `2c6cf8114b39ba83f105521604ca158432874afc981cd7fffc78b809e064d209`. Console SHA-256 `06fc0af22c6f9676e570d9e0d3869664573f8643ff9a850c1a579c2bb263cc93`. Capture UTC 10:15:13–10:16:15; ordinary-production restore finished 10:16:23. Raw capture and normal boot are separate files; no flash write command was used.

## MEASURED

- AK09911: 398 fresh XYZ samples in a 40.042233-second acquisition window. No reported I/O error, overflow, invalid record or DOR. Power-down returned 0, device close ESP_OK. ID 48 05, ASA 17 17 13, fuse-mode readback 00: adjustment validity remains UNKNOWN; raw counts only.
- QMI: 3810 accelerometer XYZ samples in 3121 successful FIFO batches (2432 single-sample and 689 double-sample). Stream records have contiguous batch/index numbering, matching counts, ordered host timestamps and no overflow flag. Acquisition result=0. This proves paired raw acquisition on this watch for this run.
- QMI initial state was idle: CTRL1=20, CTRL2=00, CTRL3=00, CTRL5=00, CTRL7=00, CTRL8=00, watermark=00, FIFO=00, steps=0. The driver used its temporary +/-8g, 125Hz accelerometer-only profile, consuming 6-byte FIFO groups. All eight final control bytes match the initial snapshot, steps remains 0. This run does not exercise an active pedometer or 12-byte gyro+accelerometer FIFO path.
- QMI cleanup returned **stop=4 (InvalidData)**; pre-reset discarded_fifo_words=3; device close=ESP_OK. The final matching control snapshot does not turn this cleanup result into PASS. The stop path aggregates the first mismatch and does not log its stage, expected/observed byte or post-reset remaining count. The exact cause is therefore UNKNOWN.
- Ordinary production was restored and its actual 8-second serial log shows SPI_FAST_FLASH_BOOT, ELF `d5e09572c`, no USB watch-control endpoint, UI ready and alive through 6 seconds. USB was closed; fuser subsequently reported no holder. This boot evidence is not a new visual UI acceptance test. The ELF prefix is what the log shows, not a checked expectation: this archive's `LOAD_RESULT.json` records no `expected_ordinary_elf_prefix`, so `verify.py` reports that field `UNKNOWN` rather than matching a literal. The three Waveshare archives here restored three different production images, so a default would have checked this one against an unrelated run.

## Timing interpretation

Host request-to-release was 4820–6513 us (mean 5127.353 us). Request-to-freeze-confirm was 1645–2797 us; freeze-confirm-to-release was 3175–3717 us. These are host transaction boundaries, not conversion timestamps or exact frozen intervals. FIFO freezing can discard new conversions per the primary datasheet; the exact discarded conversion count is UNKNOWN. 3810 samples over the 39.994820-second first-request/last-release span is approximately 95.26 delivered samples/second, not proof of a 95.26Hz sensor ODR or lossless 125Hz capture. AK host intervals were 61375–115968 us; these are receipt timing, not sensing-time jitter.

## Evidence boundaries and next action

Mount fastening is owner-confirmed, but sensor rigidity, sensor-to-watch rotation, precise pose and reference direction were not measured. The owner was asked to leave the watch in place. This record is not an accuracy, calibration, tilt-compensation or repeatability result. Earlier demonstrated magnetometer motion response is not retracted by these limits. Motor testing remains later, after a working calibrated compass.

Primary QST 13-52-27 Rev A sections 5.10.6.2 and 8.10 state that FIFO reset clears data/count/flags; section 8.3 says bypass disables filling. They do not establish which stop check failed in this run. Do not replace this unknown with a guessed delay or suppress the check. Next: add only bounded stop-stage/readback evidence in #515 under the next ordinary writer claim, then repeat the stationary capture and normal-boot restoration. Keep #450 open. No new owner movement or wiring action is required for that diagnostic run.

## Reproduce the evidence checks

Run `python3 docs/research/qmi-paired-waveshare-2026-09-09/verify.py` from the repository root. It verifies the five original file hashes and the transcript structure, then regenerates the descriptive `ANALYSIS.json`. `ANALYSIS.json` is derived rather than original and is no longer listed in `SHA256.json`: the command rewrites it, so pinning its hash there made the second run of this same command report the archive as corrupt. Structural assertions passing does not convert `qmi_cleanup_check: FAIL` into a hardware pass.

`INPUT.json` is the byte-preserved pre-execution freeze manifest. Its original NOT EXECUTED status describes that freeze time; this report and the actual console supersede it for this run. The binary, ELF, config and build transcript named by that input remain in the private frozen directory; they are identified by hashes, not represented as files in this archive. Full loader output and the capture orchestration remain in the private capture directory. No original capture byte was normalized for Git. Source code for the measured image is pinned above. The later [stop diagnostic](../qmi-stop-waveshare-2026-09-09/README.md) identifies the failed count check; it does not turn this original cleanup into PASS.
