# Follow-up refused entry — 2026-09-09

MEASURED on the same Waveshare, source `f4dcfbb4cb2c0df07e5d3ae4e8712cf0ca12b6e1`. RAM binary 189840 bytes, SHA-256 `b55c813a477d0ef721911877f44d5582fd3fd7a53fa1e8e76d3e31046d462ccb`. The image ran 14:07:39–14:08:41 UTC; ordinary boot restoration finished 14:08:49 UTC.

QMI returned `start=8` (Busy) before acquisition. Both sample counts are zero. The initial snapshot is complete and idle (CTRL1=20, other saved controls zero, steps=0). The final snapshot is incomplete because the driver never took ownership. `stop=0` is the unowned no-op, not proof of successful cleanup. Post-reset and final observer validity are both zero, so their zero values are not measurements of an empty FIFO.

This guard can reject an outstanding command, CmdDone, an active FIFO control or a nonzero count. The initial FIFO control is zero; this image did not expose the remaining predicates. The precise reason is UNKNOWN. Ordinary MCU boot did not establish an admissible QMI entry state. No guard is removed and no nonempty FIFO is taken over. The next read-only observation exposes count/status and CTRL9/STATUSINT after stop, including when a complete initial snapshot was followed by Busy; it does not send reset, acknowledgement or any other command on that refused path.

Eight seconds of raw normal-boot capture verify ordinary ELF `2915714b7`, no USB watch-control endpoint, saved brightness 5% and alive through six seconds without panic. This is not visual acceptance. The paired-stream verifier intentionally rejects this archive because neither stream ran. SHA256.json identifies the five preserved original files; INPUT.json is the pre-run freeze manifest, not an executed-success claim.
