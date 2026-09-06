# The T-Watch's own GNSS through the product path — bench log, 2026-09-06

[#436](https://github.com/hleserg/Attadipa/issues/436) established what the part
is. This is the next question and a different image:
[#442](https://github.com/hleserg/Attadipa/issues/442) asks whether the parser
this repository already had is fed by that module on that board — NMEA →
`gnss::NmeaReceiver` → `core::LocationService` → one log line, no UI.

The capture is from the bench watch on 2026-09-06, indoors, on the desk, with no
sky, on USB power. The build is `CONFIG_ATTADIPA_GNSS_LOCAL=y` with `_RX=41` and
`_BAUD=38400`, and `CONFIG_ATTADIPA_GNSS_BRIDGE=n`. The raw capture is
`~/attadipa-bench/i442/twatch_gnsslocal_boot_2026-09-06.log`, 73 lines, kept off
this repository with the other bench logs; the six that carry a result are
transcribed whole below, and the other 67 are the main task's `alive` heartbeat.

## 1. The lines — MEASURED

```
I (16384) gnss: listening on GPIO 41 at 38400 baud, NMEA, receive only
I (16384) board-power: AXP2101: LDO enable -> 0x17 (BLDO1 3.3 V, GNSS)
I (16384) board-power: AXP2101: DC enable 0x19 (DC3 off) -- read, not written
I (16434) gnss: avail unreachable src Unknown fix Unknown validity NoFix position none
I (17434) gnss: avail ready src Unknown fix Unknown validity NoFix position none
I (18434) gnss: avail ready src LocalGnss fix NoFix validity NoFix position none
```

No `W` and no `E` line appears anywhere in the 73. The capture was started after
the reset that followed flashing, so its first line is a partial `esp_image`
line and the boot banner is not in it.

## 2. What that proves

**The module emitted at least one checksum-valid NMEA sentence and this end
framed it.** `avail ready` is not reachable without one, because the flag it
reads is set only past a strict checksum:

- `gnss/src/nmea_receiver.cpp:186` — "if (!minmea_check(line_, true)) {"
- `gnss/src/nmea_receiver.cpp:191` — "heard_ = true;"
- `gnss/src/nmea_receiver.cpp:409` — "if (!heard_) return Availability::Unreachable;"

**At least two `RMC` sentences were parsed by `minmea`, not merely framed.** The
published state carries `src LocalGnss`, that field is stamped in exactly one
place — inside the `RMC` branch, after its parse returned true — and the epoch
it opens is published by the *next* `RMC` and by nothing else, `close_epoch()`
having one caller:

- `gnss/src/nmea_receiver.cpp:207` — "open_.source = core::PositionSource::LocalGnss;"
- `gnss/src/nmea_receiver.cpp:203` — "close_epoch();"

**The timing agrees with a 1 Hz `RMC` against a 1 s tick.** The three state
lines are 1000 ms apart, which is the tick's period, and the epoch closes one
tick after the first sentence was heard:

- `firmware/main/twatch_board.cpp:50` — "constexpr std::uint32_t kGnssTickMs = 1000;"

**The rail came up in this build too.** `LDO enable -> 0x17` is #442's scope
item 4 doing its work, and DC3 is still only read, never written.

**`NoFix` is the honest answer here rather than a placeholder.** No coordinate
is held and none is claimed: `position none` for the whole capture, indoors,
which is what the Definition of Done asked the indoor run to show.

## 3. What it does not prove

**Nothing here says sentences kept arriving after 18434 ms.** The absence of a
further state line is consistent with two different worlds, and this log cannot
separate them: the silence timeout is measured against a clock the tick itself
hands in, so a stopped tick freezes `now_` and `last_sentence_` together and
`Ready` survives a dead line exactly as it survives a live one.

- `gnss/src/nmea_receiver.cpp:410` — "core::elapsed(last_sentence_, now_) < silence_after_"
- `gnss/src/nmea_receiver.cpp:127` — "now_ = now;"
- `firmware/main/local_gnss.cpp:381` — "// Still tell the receiver what time it is: its silence timeout is"

The 67 `alive` lines are not the missing witness, and it is worth saying why
before somebody reads them as one. They are logged by the main task; the GNSS
tick is an LVGL timer on another:

- `firmware/main/twatch_board.cpp:765` — "[](lv_timer_t *) { attadipa::firmware::local_gnss_tick(); },"

**And no position ever reached a `LocationState` on this board.** A parser shown
reading `RMC` with its validity flag clear has not been shown to build a
coordinate.

## 4. What is still owed, and the one run that settles it

`NOT EXECUTED — HARDWARE REQUIRED`: the same image under sky. A fix logs a
*change* — `fix` and `validity` move, `position` becomes `held` — and that
single line closes both gaps in §3 at once, because a state line can only be
printed by a tick that is still running.

That log is safe to hand over as it stands. The `ESP_LOGI` line says whether a
position is held and never what it is; the engineering line that would carry the
coordinate is at `DEBUG`, and no code in this firmware raises this tag.
