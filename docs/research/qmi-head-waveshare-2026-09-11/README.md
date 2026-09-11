# The drain executed, and what it froze was nothing — 2026-09-11

Waveshare ESP32-S3-Touch-AMOLED-2.06, MAC `28:84:85:b2:18:a4`, USB-powered with
no battery, lying untouched for both runs. `PURE_RAM_APP` loaded through
`tools/flash/ramhold.py`; no flash write command was issued, and the production
image came back with a hard reset and is captured here.

The review of #515 asked for the head on a board, and this archive is that. It
answers more than it was meant to: the entry transition **empties the FIFO**, so
the drain freezes zero words while the entry count says three — the exact case
the round-4 fix was written for, measured rather than modelled.

## Provenance — one image, two loads

| capture | source | binary | segments, bytes |
|---|---|---|---|
| `console-head-A.txt`, `console-head-B.txt` | `102be3d4` (#515) — the commit whose message names these runs; the edits it makes to comments after the load leave the binary byte-identical, which the hash beside it is the check on | 190592, SHA-256 `54ca8cacf00fbc9a85f529596b53ca56f703a0b61299977fa34b90ed3c99f758` | 63928 / 464 / 126076 / 32 |
| `NORMAL_BOOT.txt` | the unit's own flash image | — | — |

`sdkconfig` SHA-256 `78f28143efa1f788cba52e68cdb02eff0520f3a5c2f28ad7dc7a4e62cb8928a7`
— byte-identical to the `build-drain` configuration of the [earlier
session](../ak09911-drain-waveshare-2026-09-11/README.md), so the two archives
differ by source, not by options. `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP`,
`CONFIG_ATTADIPA_QMI8658_PAIRED_PROBE`, `CONFIG_ATTADIPA_QMI8658_DRAIN_STALE_FIFO`
and `CONFIG_ATTADIPA_AK09911_PROBE` all set. The binary and configuration live in
the private build directory and are identified here by hash, as in both siblings.

## What this archive retracts

**An earlier pair of runs the same day, committed as `bc4c7074` and replaced
here, was read as "the FIFO was empty at entry". That reading was wrong.** Those
runs used the image before `e0e2ea78`, whose log block was guarded on the word
count: a drain that froze zero words printed nothing, and the transcript of a
drain that moved nothing was byte-for-byte the transcript of a boot that found
an empty FIFO. The review of #515 named that ambiguity — `drain-entry-count-unlogged`,
round 5 — before the runs below were taken, and it was right. The entry count and
a `drained()` flag now reach the log, and with them the same board says the
opposite of what was inferred from its silence.

## MEASURED — the residue is there, and the transition is what clears it

Both runs, identical:

```text
QMI before CTRL1=20 CTRL2=00 CTRL3=00 CTRL5=00 CTRL7=00 CTRL8=00 WTM=00 FIFO=00 steps=0
QMI start=0 temporary_accel=1 frame_bytes=6 entry_fifo_words=3 drain_built=1; raw axes only
QMI drained stale_words=0 (entry succeeded)
```

Three words waiting at entry, in bypass, exactly as the [read-only entry
observation of 2026-09-09](../qmi-stop-waveshare-2026-09-09/persistent-fifo/README.md)
found them. The drain ran. It wrote `FIFO_CTRL = 01`, issued `REQ_FIFO`, and the
count it then read back was **zero** — and the re-count in `start()` agreed, so
entry succeeded with nothing read from `0x17` at all.

**So on this part the bypass-to-FIFO transition empties the queue.** That was
UNKNOWN when `ea57bbf5` was written, and the commit sized the payload from the
frozen count precisely because it could not be assumed equal to the entry count.
On this board the two differ by three words on every run.

**And the three `00 80` words of the earlier archive were filler, not residue.**
The image that produced them sized its read from the entry count and read six
bytes out of a FIFO the transition had already emptied; `0x8000` is what `0x17`
returns with nothing queued, which is also what the refused bypass attempt read
without moving anything. The earlier archive already declined to call them
residue; this run says what they are.

## MEASURED — the paired acquisition, on the head

| run | QMI samples | batches | AK samples | duration | flags |
| --- | --- | --- | --- | --- | --- |
| A | 3809 | 3118 | 398 | 40.047739 s | `result=0` both, `overflow=0 invalid=0 dor=0` |
| B | 3812 | 3118 | 398 | 40.047735 s | `result=0` both, `overflow=0 invalid=0 dor=0` |

`st1=01 st2=00` on all 796 magnetometer frames. The accepted capture of
`b4a8f45b` was 3814 QMI samples in 3114 batches and 398 AK samples over
40.047958 s, so the head reproduces it.

`stop=4` with `remaining_words=3` is produced at the end of both runs, unchanged
and still UNKNOWN. What is now measured is the other half: the state it leaves
is cleared by the next owner's entry transition, and the drain is what makes
that entry legal rather than a `Busy` refusal.

## MEASURED — six cold loads agree on the stationary field

Raw counts, the watch never moved. The first four rows are the earlier session,
repeated here so the comparison is on one page.

| run | n | mean X | mean Y | mean Z | σ X | σ Y | σ Z |
| --- | --- | --- | --- | --- | --- | --- | --- |
| stationary A (earlier) | 199 | 44.86 | 66.50 | -266.89 | 1.21 | 1.09 | 1.24 |
| stationary B (earlier) | 199 | 44.86 | 66.51 | -266.99 | 1.11 | 1.16 | 1.31 |
| stationary C (earlier) | 199 | 45.11 | 67.12 | -267.49 | 1.21 | 1.23 | 1.25 |
| paired (earlier) | 398 | 44.85 | 66.33 | -266.16 | 1.24 | 1.27 | 1.28 |
| head A | 398 | 44.80 | 65.82 | -266.61 | 1.25 | 1.24 | 1.19 |
| head B | 398 | 44.71 | 65.93 | -266.68 | 1.23 | 1.24 | 1.22 |

Mean spread across all six is at most 1.30 counts on any axis and `|B|` stays
between 273.8 and 281.9 per sample. This revives nothing: the hard-iron
retraction and the owner tasks it produced (#528, #529, #530, #531) stand. It
says the field near this bench has not moved since the earlier session.

## NOT EXECUTED — HARDWARE REQUIRED

- **A drain that actually carries words out.** Every execution on this board
  freezes zero, because the transition empties the queue first. The path that
  reads `0x17` and reports a nonzero `stale_words()` has only the transport
  model in `tests/test_qmi8658_fifo.cpp` behind it. A part that keeps its count
  across the transition would exercise it; this one does not.
- **Compass, tilt, calibration, step count.** None of them, as before.

## Restoration

Ordinary firmware ELF `2915714b7` came back on a hard reset and is alive through
eleven seconds with no panic (`NORMAL_BOOT.txt`). No flash write command was
issued at any point in this session.

`verify.py` re-checks every number above against the two streams and the
manifest; `SHA256.json` identifies the three files.
