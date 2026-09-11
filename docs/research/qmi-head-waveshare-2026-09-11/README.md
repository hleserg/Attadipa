# The head on the bench, and a residue that no longer waits at entry — 2026-09-11

Waveshare ESP32-S3-Touch-AMOLED-2.06, MAC `28:84:85:b2:18:a4`, USB-powered with
no battery, lying untouched for both runs. `PURE_RAM_APP` loaded through
`tools/flash/ramhold.py`; no flash write command was issued, and the production
image came back with a hard reset and is captured here.

This archive exists because the review of #515 asked a fair question: the
[drain session](../ak09911-drain-waveshare-2026-09-11/README.md) was produced by
`b4a8f45b`, and the entry sequence has changed twice since. So the head ran.

## Provenance — one image, two loads

| capture | source | binary | segments, bytes |
|---|---|---|---|
| `console-head-A.txt`, `console-head-B.txt` | `ea57bbf5` (#515), `build-drain` | 190528, SHA-256 `22156591191cd3db0b2d3ee3de546994d8882955a7bbd1d48302e6ec9c365430` | 63896 / 464 / 126040 / 32 |
| `NORMAL_BOOT.txt` | the unit's own flash image | — | — |

`sdkconfig` SHA-256 `78f28143efa1f788cba52e68cdb02eff0520f3a5c2f28ad7dc7a4e62cb8928a7`
— byte-identical to the `build-drain` configuration of the earlier session, so
the two archives differ by source, not by options. `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP`,
`CONFIG_ATTADIPA_QMI8658_PAIRED_PROBE`, `CONFIG_ATTADIPA_QMI8658_DRAIN_STALE_FIFO`
and `CONFIG_ATTADIPA_AK09911_PROBE` all set. The binary and configuration live in
the private build directory and are identified here by hash, as in both siblings.

## MEASURED — the paired acquisition reproduces on the head

Two cold loads, back to back, `A` then `B`:

| run | QMI samples | batches | AK samples | duration | flags |
| --- | --- | --- | --- | --- | --- |
| A | 3814 | 3116 | 398 | 40.048017 s | `result=0` both, `overflow=0 invalid=0 dor=0` |
| B | 3809 | 3118 | 398 | 40.048062 s | `result=0` both, `overflow=0 invalid=0 dor=0` |

`st1=01 st2=00` on all 796 magnetometer frames. The accepted capture of the
earlier session was 3814 QMI samples in 3114 batches and 398 AK samples over
40.047958 s, so the head reproduces it.

## MEASURED — six cold loads now agree on the stationary field

Raw counts, the watch never moved. The first four rows are the earlier session,
repeated here so the comparison is on one page; the last two are this one.

| run | n | mean X | mean Y | mean Z | σ X | σ Y | σ Z |
| --- | --- | --- | --- | --- | --- | --- | --- |
| stationary A (09-11, earlier) | 199 | 44.86 | 66.50 | -266.89 | 1.21 | 1.09 | 1.24 |
| stationary B (09-11, earlier) | 199 | 44.86 | 66.51 | -266.99 | 1.11 | 1.16 | 1.31 |
| stationary C (09-11, earlier) | 199 | 45.11 | 67.12 | -267.49 | 1.21 | 1.23 | 1.25 |
| paired (09-11, earlier) | 398 | 44.85 | 66.33 | -266.16 | 1.24 | 1.27 | 1.28 |
| head A | 398 | 44.88 | 65.99 | -266.62 | 1.21 | 1.27 | 1.21 |
| head B | 398 | 44.84 | 65.85 | -266.47 | 1.19 | 1.18 | 1.25 |

Mean spread across all six is at most 1.27 counts on any axis and `|B|` stays
between 277.9 and 279.5. This does **not** revive the withdrawn hard-iron
estimate: it says the field near this bench has not moved since the earlier
session, not that any calibration number is established. The retraction and the
owner tasks it produced (#528, #529, #530, #531) stand.

## MEASURED — the entry residue did not survive either load

Both runs entered with the FIFO empty:

```text
QMI before CTRL1=20 CTRL2=00 CTRL3=00 CTRL5=00 CTRL7=00 CTRL8=00 WTM=00 FIFO=00 steps=0
QMI start=0 temporary_accel=1 frame_bytes=6; raw axes only
```

No `QMI drained` line in either stream, because `start()` found a zero count and
never called the drain. Run A ended the way every paired run ends —

```text
QMI stop_check step=count_after_reset remaining_valid=1 remaining_words=3 remaining_status=50 ...
QMI summary samples=3814 batches=3116 result=0 stop=4 discarded_fifo_words=3 close=ESP_OK
```

— with three words the stop could not clear, and run B, loaded minutes later,
still found the count at zero.

**That is a change from what this repository recorded, and it is the honest
reading of it.** The [read-only entry observation of
2026-09-09](../qmi-stop-waveshare-2026-09-09/persistent-fifo/README.md) found
count=3, status=50 after ordinary MCU reboots, and the earlier drain session
began in that state. It is not the state the board is in now. What cleared it
between a stop that reports three words and the next entry is **UNKNOWN**: the
loader's reset, the sensor's own behaviour and the intervening production boot
are all in the gap, and nothing here separates them.

`stop=4` with `remaining_words=3` is unchanged and still UNKNOWN. What this pair
of runs shows is that the state it leaves no longer blocks the next entry on
this board — which is the same outcome the drain was built for, reached without
the drain.

## NOT EXECUTED — HARDWARE REQUIRED

- **The drain path.** `Qmi8658Fifo::drain_stale()` has never run on a board in
  the form the head carries: `1df9fcfb` moved the CTRL8 handshake write ahead of
  the drain's own CTRL9 command, and `ea57bbf5` made the payload size come from
  the count re-read after `REQ_FIFO`. Both are covered by the transport model in
  `tests/test_qmi8658_fifo.cpp` and by nothing physical. Executing them needs a
  board that presents a nonempty FIFO at entry, and this board no longer does.
- **Compass, tilt, calibration, step count.** None of them, as before.

## Restoration

Ordinary firmware ELF `2915714b7` came back on a hard reset and is alive through
eleven seconds with no panic (`NORMAL_BOOT.txt`). No flash write command was
issued at any point in this session.

`verify.py` re-checks every number above against the two streams and the
manifest; `SHA256.json` identifies the three files.
