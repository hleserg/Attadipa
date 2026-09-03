# T-Watch S3 Plus — PCF8563, input and wake sources

Desk research for issue #422. Nothing here was executed on hardware: every
level, latch, clear and wake-from-light-sleep row below is
`NOT EXECUTED — HARDWARE REQUIRED`. Section 4 is the bench procedure that would
settle what is left open, section 5 is the executable scope this report
proposes, and section 6 covers the two remaining upstream candidates, the ADRs
and how the board reaches #367 without a second power owner.

The board under discussion is the **LilyGO T-Watch S3 Plus** only. Nothing here
transfers to the Waveshare board, which carries a **PCF85063** with a different
register base and no century bit.

## Sources

| Source | Identity |
| --- | --- |
| NXP PCF8563 datasheet Rev. 11.1, 19 Jan 2026 | `https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf`, SHA-256 `2ed5d1b0a1051d0e81399417c4ff0ced0658fa36e13c0487670f9cb6dfb4f1d4` — the value already recorded in `docs/research/RTC_SLOW_CLOCK.md` |
| T-Watch S3 Plus schematic | `T_WATCH-S3 25-03-24.pdf`, SHA-256 `3fc71eba5b30085b4fe20c6222df26230af0602ddff08e257b9cdf090c58d931`. **Rendered at 300 dpi, not text-dumped**: the PDF text layer loses every wire |
| SensorLib | `lewisxhe/SensorLib@2b9e591f245e447d3d00ec8798c3f49b897882d9` (0.4.1, 2026-07-30); `src/time/pcf8563/SensorPCF8563.hpp` SHA-256 `12b6cf2ac74aad6f5e0b6c3e17f6b198aa331c90d6ea2145c023b4e164963caa` |
| Vendor firmware, evidence level 3 — it ships on this board | `LilyGoWatchS3` `38e6f8d` |
| AXP2101 datasheet V1.4 EN | local extract, not committable |
| ESP-IDF v5.5.5 | `esp_sleep.h` and `soc/esp32s3/include/soc/soc_caps.h` at `b774170ff46c393eeb5e495ea37936038d3f4f4f` |
| ESP32-S3 datasheet v2.2 | §5.4 Table 5-4, p.65 |

## 1. PCF8563 protocol truth

Everything in this section is a datasheet fact unless the row says otherwise.

### 1.1 Register map

Table 4 "Register overview", pp.10-11.

| Addr | Name | Bit 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 00h | Control_status_1 | TEST1 | 0 | STOP | 0 | TESTC | 0 | 0 | 0 |
| 01h | Control_status_2 | 0 | 0 | 0 | TI_TP | AF | TF | AIE | TIE |
| 02h | VL_seconds | **VL** | SECONDS 0-59 BCD | | | | | | |
| 03h | Minutes | x | MINUTES 0-59 BCD | | | | | | |
| 04h | Hours | x | x | HOURS 0-23 BCD | | | | | |
| 05h | Days | x | x | DAYS 1-31 BCD | | | | | |
| 06h | Weekdays | x | x | x | x | x | WEEKDAYS 0-6 binary | | |
| 07h | Century_months | **C** | x | x | MONTHS 1-12 BCD | | | | |
| 08h | Years | YEARS 0-99 BCD | | | | | | | |
| 09h | Minute_alarm | **AE_M** | MINUTE_ALARM 0-59 | | | | | | |
| 0Ah | Hour_alarm | **AE_H** | x | HOUR_ALARM 0-23 | | | | | |
| 0Bh | Day_alarm | **AE_D** | x | DAY_ALARM 1-31 | | | | | |
| 0Ch | Weekday_alarm | **AE_W** | x | x | x | x | WEEKDAY_ALARM 0-6 | | |
| 0Dh | CLKOUT_control | FE | x | x | x | x | x | FD1 | FD0 |
| 0Eh | Timer_control | TE | x | x | x | x | x | TD1 | TD0 |
| 0Fh | Timer | countdown value n, 8-bit binary | | | | | | | |

The address pointer auto-increments and wraps from 0Fh to 00h, so a 7-byte burst
from 02h reads the whole calendar in one transaction.

**The load-bearing detail the map hides:** the alarm-enable bits are *disable*
bits. §8.6 and Table 27: `AE_x = 1` means "this register is ignored". A register
with `AE_x = 0` participates in the match.

### 1.2 The VL flag

| Claim | Evidence |
| --- | --- |
| VL is bit 7 of register 02h, sharing the byte with the seconds field | Table 8, p.13 |
| Its power-on value is **1** | Table 27 "Register reset values", p.23 — 02h bit 7 shows `1`, every other bit `x` |
| Hardware sets it and hardware never clears it: "The VL flag can only be cleared by using the interface" | §8.4.1.1, p.14 |
| It sets whenever the oscillator stopped or VDD fell below Vlow | §8.4.1.1, p.14 |
| Vlow is typ 0.9 V, max 1.0 V | Table 29 "Static characteristics", p.31 |

**What this means for trusting a read.** `VL = 1` says the time in the same
transaction is not merely stale but *unqualified*: the counters may have missed
an unknown number of ticks, so the read must be rejected outright rather than
aged. `VL = 0` is the only positive statement the part makes about its own
integrity. Because VL lives inside the seconds register, the integrity flag and
the time it qualifies arrive in the same byte — a driver that reads them in two
transactions has given the guarantee away for free. See defect D4.

### 1.3 BCD and century

Registers 02h-05h, 07h low five bits, 08h and 09h-0Bh are packed BCD; 06h and
0Ch are plain binary 0-6; 0Fh is plain binary. The upper `x` bits are explicitly
don't-care on read, so masking before conversion is mandatory: a value like
`0x1A` on a bus fault is not valid BCD and decodes to nonsense. Validity
checking is the driver's job, because the part does not do it.

Table 15, p.15 gives the century bit as relative, not absolute: `C = 0` "It
indicates that the century is x."; `C = 1` "It indicates that the century is
x + 1." Footnote [1] adds that C toggles when Years overflows 99 to 00.

**The datasheet never names x.** The mapping from C to an absolute century is
entirely a firmware convention, and Table 27, p.23 gives 07h reset value `x` for
every bit including C — so C is undefined after power-on and is evidence of
nothing until firmware has written it once. In practice `VL = 1` already
condemns the whole calendar, so a driver that rejects on VL never has to
interpret an undefined C.

**This board already carries a convention.** The shipped vendor firmware uses
SensorLib, whose `getDateTime()` maps `C = 1` to 1900 and `C = 0` to 2000. A
replacement driver that picks the opposite convention misreads a coin-cell-backed
calendar written by the factory image by a hundred years, silently. That is a
compatibility constraint, not a datasheet fact, and bench step B1 settles which
convention this unit actually holds.

### 1.4 Alarm registers

Four registers, 09h-0Ch, each with its own `AE_x` disable bit in bit 7. Table 27,
p.23: all four reset to `AE_x = 1`, so every alarm condition is disabled out of
reset. §8.6.5, p.19: "Alarm registers having their AE_x bit at logic 1 are
ignored." Enabled registers are ANDed, so an alarm with only the minute enabled
fires every hour on that minute.

**Resolution is one minute, and the match is on edge, not level.** Figure 9
caption, p.19: "It's only on increment to a matched case that the alarm flag is
set." Writing an alarm equal to the current minute, after that minute has
already incremented, never fires — arming code must target the next minute
boundary. The sub-minute mechanism is the countdown timer, not the alarm.

§8.6.5 also fixes the clear semantics: "AF remains set until cleared by the
interface. Once AF has been cleared, it only sets again when the time increments
to match the alarm condition once more."

### 1.5 AF and TF, and clearing one without clobbering the other

Both flags live in register 01h: AF is bit 3, TF is bit 2.

Table 6, pp.11-12: for both flags, writing `0` clears and writing `1` leaves the
flag unchanged. §8.3.2.1, p.12: "To prevent one flag being overwritten while
clearing another, a logic AND is performed during a write access."

So the write semantics are AND-on-write, inverted: **write 0 to clear, write 1 to
preserve.** The correct clear of AF alone is therefore not a read-modify-write
but an unconditional write of a byte with `AF = 0` and `TF = 1`, plus the
intended TI_TP, AIE and TIE bits. A read-modify-write that writes TF back as it
was read loses any TF that set between the read and the write. That is a real,
silent, timing-dependent loss, and SensorLib contains it — defect D5.

### 1.6 CLKOUT and the countdown timer

**0Dh CLKOUT_control**: `FE` bit 7 enables the output, `FD[1:0]` selects
32.768 kHz / 1024 Hz / 32 Hz / 1 Hz. Table 27, p.23: reset is `FE = 1`,
`FD = 00`, so **32.768 kHz is on by default** — and it costs backup-battery
current for nothing here, because CLKOUT reaches no ESP32 pin:
`docs/research/RTC_SLOW_CLOCK.md:15` — "but no ESP32 pin". The vendor firmware disables it explicitly and gives that same
reason in a comment about conserving backup-battery current.

**0Eh Timer_control**: `TE` bit 7 enables the countdown, `TD[1:0]` selects
4096 Hz / 64 Hz / 1 Hz / 1/60 Hz. Reset is `TE = 0`, `TD = 11`. TD must be
*written*, not ORed, precisely because it resets to `11` — defect D6 is what
happens when it is ORed.

**0Fh Timer** holds an 8-bit binary n; the period is n divided by the source
frequency.

**TI_TP, 01h bit 4** picks the INT waveform for the timer: `0` makes INT follow
TF as a latched level, `1` makes it a pulse. Table 7, p.13 gives the widths —
1/8192 s at n = 1 and 1/4096 s at n > 1 on the 4096 Hz source, 1/128 s and
1/64 s on 64 Hz, 1/64 s on 1 Hz and 1/60 Hz. TI_TP does not affect the alarm,
whose INT is always a level.

### 1.7 INT pin behaviour, and what it costs a wake source

| Claim | Evidence |
| --- | --- |
| INT is open-drain, active LOW | Table 3 "Pin description", p.8 — "INT ... Interrupt output (open-drain; active LOW)" |
| With both enables off the pin is not driven at all | Figure 5 caption, p.12 — "When bits TIE and AIE are disabled, pin INT remains high-impedance." |
| Drive strength IOL min 1 mA at VOL 0.4 V | Table 29, p.32 |
| CLKOUT is likewise open-drain | Table 3, p.8 |

**This is the fact that governs section 3.** An alarm interrupt here is a
latched active-low *level*, not a pulse: it goes low on match and stays low
until an I2C transaction clears AF. Three consequences follow. A level-triggered
wake source on this pin is armed correctly and will fire. The same source cannot
be re-armed after the wake until AF has been cleared, because the line is still
low — arming and re-sleeping without the clear produces an instant re-wake, a
spin rather than a sleep. And an edge-triggered interrupt fires exactly once and
then never again, for the same reason.

The one exception is the countdown timer with `TI_TP = 1`, whose pulse can be as
short as 1/8192 s: a level-triggered wake may miss it entirely. **For wake, use
`TI_TP = 0` and clear the flag on wake.**

### 1.8 The interface watchdog is a correctness trap, not a footnote

§9.6, p.28: "If the interface is active for more than 1 s from the time a valid
target address is transmitted, then the PCF8563 automatically clears the
interface ... Each time the watchdog period is exceeded, 1 s is lost from the
time counters."

Any driver that holds the bus across a log call, a mutex wait or a debugger
breakpoint inside a transaction silently loses a second of wall time. That makes
the single-burst calendar read not merely a §8.5 nicety but the safe shape —
and §8.5, p.17 says so directly: "setting or reading seconds through to years
must be made in one single access", because the carry propagates between one
register read and the next.

### 1.9 I2C address

§9.5.1, pp.26-27 gives read address A3h and write address A2h, so the 7-bit
address is **0x51**, fixed, with no address-select pin. That matches
`docs/research/HARDWARE_MATRIX.md:99` — "main I2C, INT 17 | 0x51 | VBACKUP (coin cell)".

### 1.10 What section 1 still cannot answer

Every row above is a datasheet fact about the part. What the datasheet cannot
say about *this unit*: the assembled board's actual INT pull-up strength and
idle voltage, which section 3 derives from the schematic but nobody has
measured; whether the coin cell holds the oscillator across a full power cycle,
i.e. whether VL really is 0 after a cold boot; and the real oscillator drift,
which the crystal and its load capacitors determine rather than the part.

## 2. SensorLib 0.4.1 — fitness verdict

`src/time/pcf8563/SensorPCF8563.hpp`, 512 lines at the pinned commit, verified
by SHA against the fetch.

### 2.1 The two defects the issue alleged

**`getAlarm()`, lines 202-213 — confirmed, and worse than alleged.** The issue
called it "masks only the disable bits". That is a quarter right:

- `buffer[0] & 0x80` does mask AE_M, the real disable bit of 09h — and
  `BCD2DEC(0x80)` is 80, so the minute reads back as 80 or 0.
- `buffer[1] & 0x40` and `buffer[2] & 0x40` mask **bit 6, which Tables 19 and 20
  (pp.17-18) mark `x`** — an unused bit the datasheet guarantees nothing about.
  AE_H and AE_D are bit 7.
- `buffer[3] & 0x08` masks bit 3, inside the `x` block of 0Ch. AE_W is bit 7.

The function does return an `RTC_Alarm`, but it cannot contain the alarm: the
value bits are masked away in all four cases. It does not "return no value", it
returns a plausible-looking wrong one, which is the more dangerous failure.
There is no alarm read-back in this library at all.

**`setDateTime()`, lines 139-157 — confirmed exactly as alleged.** The signature
is `void`, and the final `writeRegBuff(SEC_REG, buffer, 7);` discards its result,
so a caller cannot learn the write failed. Setting the clock is precisely the
operation whose failure must reach the caller: `TimeService` would report
`Trusted` on a write that never landed.

### 2.2 Five further defects in the same file

**D3 — `getDateTime()` line 169 discards its read and converts an uninitialised
stack buffer into a time.** `uint8_t buffer[7];` then `readRegBuff(...)` with no
check: on a bus fault the seven bytes are whatever the stack held, masked and
BCD-decoded into an `RTC_DateTime` the caller cannot distinguish from a good
read. This is worse than the dropped write, because a bad write eventually shows
up and a fabricated time does not.

**D4 — VL is masked off in the calendar read and recovered by a second
transaction.** Line 170 masks VL away; integrity then requires
`isClockIntegrityGuaranteed()`, a separate I2C access. VL can change between the
two, and the split violates §8.5's single-access rule for no gain, since VL had
already arrived in `buffer[0]`.

**D5 — `resetAlarm()` line 237 is a read-modify-write on the flag register.**
Per §1.5 that reads 01h, clears bit 3 and writes back; a TF that sets in the
window is destroyed. The correct pattern is an unconditional write with the
sibling flag forced to 1 — and **this same file already knows it**:
`clearCountdownTimer()` at lines 412-420 does exactly that. The alarm path was
simply not held to the timer path's standard.

**D6 — `setCountdownTimer()` lines 399-407 cannot change the source frequency
and never starts the timer.** `buffer[1] |= (freq & TIMER_TD10);` is OR-only
against a register whose reset value is `TD = 11`, and ORing into `11` leaves
`11`, so 4096 Hz and 1 Hz are unreachable. `TE` is never set, so the countdown
does not run. `buffer[0]` is written and unused.

**D7 — `initImpl()` lines 459-470 accepts invalid BCD, and `reset()` is empty.**
The presence check reduces to `BCD2DEC(ret & 0x7F) <= 59`, and `0x1A` is not
valid BCD but decodes to 20 and passes. `void reset() override {}` does nothing,
so a caller that resets the RTC gets silence.

**D8 — the century idiom, lines 150-155**, is
`if ((2000 % datetime.getYear()) == 2000)`. That is true exactly when the year
exceeds 2000, so for 2001-2099 it happens to produce `C = 0`, which the getter
reads back correctly. It is wrong for exactly the year 2000, and it is
**undefined behaviour — integer division by zero — for `getYear() == 0`**, which
is what a defaulted `RTC_DateTime` supplies. Accidentally correct in the range
that matters; not code to inherit.

**D9 — there is no error propagation anywhere in the file.** Not one method
returns an I2C result. That is the root of the dropped write, D3 and D7, and it
is architectural rather than a bug to patch.

### 2.3 Licence and packaging

`idf_component.yml` declares version 0.4.1 and licence MIT. The root is MIT, but
**`src/bosch/` is BSD-3-Clause** and the manifest's `files.exclude` list does not
exclude it — so a component declaring itself MIT ships a BSD-3-Clause subtree.
Both licences are permissive and compatible, so this is not a violation, but the
declared licence is not the whole obligation and BSD-3-Clause's attribution
clause would attach. Per-driver exclusion is compile-time only, so the Bosch
source ships even when compiled out. The PCF8563 path itself is clean, including
only the I2C helper and `SensorRTC.h`. This matches the existing ledger entry at
`docs/research/REUSE_LEDGER.md:69` — "github.com/lewisxhe/SensorLib".

### 2.4 Verdict: write a minimal direct driver

Not "adopt", and not "adopt behind an adapter". The reasoning is about what the
adapter would have to contain. Its job would be to give the caller an error code
from every transaction, VL in the same transaction as the calendar, an alarm
read-back, a flag clear that does not lose the sibling flag, and a working
countdown-timer configuration. The dropped write, D3, D4, D5 and D6 are each
*inside* the library method: an adapter cannot recover an error the callee never
returned, nor merge two transactions the callee performs separately. Fixing any
one of them means bypassing the method and issuing the register access directly,
and doing that for all five leaves the adapter performing every access — at
which point SensorLib contributes a header of dead code and a second licence.

**The repository already has the right shape, proven and host-tested.**
`firmware/main/pcf85063_time.h` is a header-only, constexpr, I2C-free
decode/encode pair for the other board's RTC:
`firmware/main/pcf85063_time.h:17` — "{ Valid, VoltageLow, InvalidData };" and
`firmware/main/pcf85063_time.h:41` — "constexpr RtcDecodeStatus decode_pcf85063". It already models the three
outcomes section 1 demands — a good read, a VL-rejected read and a malformed-BCD
read — it is exercised on the host at
`tests/test_time_service.cpp:260` — "CHECK(attadipa::firmware::decode_pcf85063(raw_rtc, rtc) ==",
and it is consumed by the board layer at
`firmware/main/waveshare_board.cpp:194` — "*status = attadipa::firmware::decode_pcf85063(raw, *time);"
and by `firmware/main/provision_time.h:23` — "pcf85063_time.h". A
`decode_pcf8563` twin is a few dozen lines of pure function plus a burst read in
the board file.

Cost, `ESTIMATED` and not measured: roughly 120-160 lines of new header plus a
table-driven host test, with no new component, no new licence obligation and no
dependency flash. Against that, adopting SensorLib adds a component whose
PCF8563 path needs five bypasses.

**Do not copy `pcf85063_time.h`'s register offsets.** The PCF85063's seconds sit
at 0x04 and it has no century bit; the PCF8563's sit at 0x02 and it does. The
two headers share a shape, not a map — the same class of error this repository
already warns about between the two boards.

Licence obligations incurred: **none**, because nothing is taken.

## 3. Board translation — the four interrupt lines

### 3.1 What the rendered schematic shows

| Line | Finding | Sheet | Status |
| --- | --- | --- | --- |
| RTC INT | U45 PCF8563 pin 3 `INT` goes to net `IO17`, and **R288 10 kΩ pulls that net to `+3V3`** | 3 | VERIFIED from the schematic. **Not recorded in `HARDWARE_MATRIX.md`**, which names the pin but not the pull-up |
| BMA423 INT1 | U24 pin 5 `INT1` goes to net `IO14` with **no pull resistor anywhere on the net**; pin 6 `INT2` is an unrouted stub | 4 | VERIFIED from the schematic, and consistent with `docs/research/HARDWARE_MATRIX.md:100` — "**INT2 is bonded out but not routed** (R12, R15 not fitted)" |
| Touch INT | Connector U5 pin 4 `INT` goes to net `IO16`, pulled up by **R551 10 kΩ to `AVDD`**; R552 and R553 pull touch SDA and SCL to `AVDD`, which comes from `LDO3` through R75 0R | 4 | VERIFIED from the schematic |
| PMU IRQ | AXP2101 pin 38 `IRQ` is pulled up by **R8226 47 kΩ to net `1.8V`**, then **R53 2 kΩ in series** to net `PMU_IRQ1` and module pin 27, GPIO21. **No 3.3 V pull-up anywhere on that net** | 1 and 2 | VERIFIED from the schematic |
| Origin of the `1.8V` net | AXP2101 **pin 28 `VRTC`** drives it; pin 27 `VBACKUP` carries `RTC_3_3V`. The datasheet pin table gives "28 VRTC P RTC power output", the feature list "RTCLDO1/2: 1.8V/2.5V/3V/3.3V, 30mA" and "Support RTCLDO1 supplied by backup battery", and §6 power states "all voltage outputs are turned off except RTCLDO" | 1 | VERIFIED from schematic and datasheet |

**Why the last row matters.** The pull-up on GPIO21 is the PMU's own always-on
RTC LDO, which survives PMU standby and can be backup-battery fed. That is
deliberate: the AXP2101's logic-I/O table specifies a 1.8 V pull-up rail with
VIH 1.3 V and VIL 0.8 V, so the pull-up matches the *PMU's* input spec.

It does not match the *SoC's*. ESP32-S3 datasheet v2.2 §5.4 Table 5-4, p.65 gives
VIH min 0.75 × VDD = 2.475 V, VIL max 0.25 × VDD = 0.825 V, and an internal weak
pull-up of 45 kΩ typical with no min or max.

- With the internal pull-up **off** and the PMU IRQ high-impedance, GPIO21 idles
  near 1.8 V — above VIL max and below VIH min, in the **indeterminate band**.
  The line may read as permanently asserted. `ESTIMATED`.
- With the internal 45 kΩ pull-up **on**, the divider of 3.3 V through 45 kΩ
  against 1.8 V through 47 kΩ settles near **2.58 V**, about 0.1 V above VIH min
  on a typical-only resistor spec. Marginal but plausible. `ESTIMATED`.

The vendor firmware confirms the concern and shows the mitigation: it holds the
PMU interrupt pin as `INPUT_PULLUP` while awake and calls `rtc_gpio_pullup_en()`
on it immediately before light sleep, enabling the pull-up specifically for the
sleeping domain. **Any Attadipa code arming GPIO21 must do the same, and must
never configure GPIO21 as an output** — the AXP2101 marks that pin `DIO`, and
holding it low for more than 16 ms is a power-on request to the PMU itself.

### 3.2 The SoC constraints that decide the design

Verified against ESP-IDF v5.5.5 source. The negatives were re-checked with
`rtk proxy`, because a lossy filter must not be trusted for an absence.

| Constraint | Evidence |
| --- | --- |
| Light-sleep GPIO wake is **level-triggered only** — "each pin can be individually configured to trigger wakeup on high or low level" — needing `gpio_wakeup_enable()` then `esp_sleep_enable_gpio_wakeup()` | `esp_sleep.h` |
| **`esp_sleep_get_gpio_wakeup_status()` does not exist on ESP32-S3**: it is guarded by `SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP`, which the S3's `soc_caps.h` never defines. Confirmed a real negative rather than a lookup failure, because the ESP32-C6's `soc_caps.h` does define it | `esp_sleep.h`, `soc_caps.h` |
| **`SOC_PM_SUPPORT_EXT1_WAKEUP_MODE_PER_PIN` is absent on the S3** and present on the C6. All EXT1 IOs therefore share one level mode, or `esp_sleep_enable_ext1_wakeup_io()` returns `ESP_ERR_NOT_ALLOWED` | same files |
| `esp_sleep_get_ext1_wakeup_status()` is **not** so guarded and returns a per-pin bitmask | `esp_sleep.h` |
| The S3's RTC GPIO range is 0-21, so GPIO14, 16, 17 and 21 are all EXT0/EXT1-capable | `soc_caps.h` |
| "Internal pullups and pulldowns don't work when RTC peripherals are shut down. In this case, external resistors need to be added." | `esp_sleep.h` |
| "the wakeup IOs will be set to the holding state before entering sleep ... please call `rtc_gpio_hold_dis` to disable the hold function" | `esp_sleep.h` |
| "This API will only return one wakeup source. If multiple wakeup sources wake up at the same time, the wakeup information may be lost." — `esp_sleep_get_wakeup_causes()` returns a bitmap instead | `esp_sleep.h` |

Two design conclusions follow immediately. **On the GPIO light-sleep path this
SoC gives no per-pin attribution**, so a wake handler must read all four GPIO
levels before touching any bus and then read each device's status register,
rather than asking the wake API which pin fired. And **on the EXT1 path every
armed pin shares one level mode**: GPIO16, 17 and 21 are active-low while GPIO14
is not, so the four lines do not compose into a single EXT1 mask.

### 3.3 GPIO14 / BMA423 is active-HIGH

This overturns the natural assumption that all four interrupts are active-low,
and it is the most consequential finding in this section. The vendor firmware
configures the sensor interrupt pin as `INPUT_PULLDOWN`, attaches on `RISING`,
and in its deep-sleep path *switches* the EXT1 mode: the default
`ESP_EXT1_WAKEUP_ANY_LOW` becomes `ESP_EXT1_WAKEUP_ANY_HIGH` when the sensor is
among the wake sources.

So the BMA423 INT1 is driven push-pull active-high, pulled down by
configuration. That is consistent with the schematic finding that `IO14` carries
no pull resistor: an open-drain active-low interrupt on that net would float, so
push-pull is the only workable choice.

The vendor firmware also demonstrates the missing per-pin capability
empirically: **it cannot arm the sensor and the power key together.** Its
light-sleep path hard-codes `ESP_EXT1_WAKEUP_ANY_LOW` and its own error message
lists only the power key and the touch panel as permitted light-sleep wake
sources. The sensor is a deep-sleep-only wake source there, and only by flipping
the global mode. This is the ESP32-S3's missing per-pin mode visible in shipping
vendor code.

`UNKNOWN`: the BMA423 register writes that set INT1's output driver mode, active
level and latch behaviour. The vendor calls `configInterrupt()` with no
arguments, so the values live inside the Bosch driver, and the BMA423 datasheet
is not in hand. The vendor firmware *configures* GPIO14 active-high — which is
`VERIFIED` about the vendor's build and about nothing else, because
`INT1_IO_CTRL (0x53)` belongs to whichever driver owns the part and Attadipa's
own may set either level. It is **H20**, not a board fact. The
*latch* behaviour — whether the line stays high until the status register is
read or self-clears — is genuinely UNKNOWN, and it decides whether a level wake
spins. What would settle it: the Bosch BMA423 datasheet sections for
`INT1_IO_CTRL (0x53)` and `INT_LATCH (0x55)`.

### 3.4 Per-line translation

Read this alongside the existing seam. Today the firmware arms exactly one GPIO
source: `firmware/main/board_power.cpp:286` — "gpio_wakeup_enable(touch_interrupt_, GPIO_INTR_LOW_LEVEL)",
and the comment below it, `firmware/main/board_power.cpp:303` — "The AXP2101 has no wake line to",
states a premise that is
**true of the Waveshare board and false of the T-Watch**, where the AXP2101 IRQ
does reach GPIO21. Whichever board that comment governs, the translation below is
new work.

**Line 1 — PCF8563 INT to GPIO17, the RTC alarm and countdown timer.**

| Row | Value | Evidence |
| --- | --- | --- |
| Polarity | Active LOW | Datasheet Table 3, p.8 |
| Drive | Open-drain, high-impedance when AIE and TIE are both 0 | Datasheet Figure 5 caption, p.12 |
| External pull | **R288 10 kΩ to `+3V3`** — `docs/research/HARDWARE_MATRIX.md:200` — "pin 18 → net `+3V3`, the rail every always-on part sits on". That description is the schematic's, and whether the rail is *always* on is **H8, `CONFLICTING`**: `docs/research/HARDWARE_MATRIX.md:209` — "**`+3V3` is a switchable rail**, and" — prices cutting it, the RTC chip included | Schematic sheet 3; rail status H8 |
| Needs an internal pull? | **Not while `+3V3` is up** — the only one of the four with a hard 3.3 V external pull-up, so it survives the *RTC peripheral domain* being powered down. It does not survive the *rail* going down, and H8 leaves open whether ALDO1 can take it down | Derived |
| Wake mode | `GPIO_INTR_LOW_LEVEL` for light sleep, or EXT0/EXT1 `ANY_LOW` | Datasheet and `esp_sleep.h` |
| Latched? | **Yes** — INT stays low until AF, or TF with `TI_TP = 0`, is cleared over I2C | Datasheet §8.6.5, p.19 |
| Mandatory wake step | Clear AF by writing 01h with **AF = 0 and TF = 1** before re-arming; skipping it produces an immediate re-wake spin | Datasheet §8.3.2.1, p.12 |
| Vendor precedent | **None** — the vendor never uses GPIO17 as a wake source, only parking it in the deep-sleep pin list | Vendor firmware |
| Behaviour on hardware | **NOT EXECUTED — HARDWARE REQUIRED** | — |

This is the strongest of the four candidates: its pull-up is external and hard
rather than internal, its polarity is unambiguous, its latch is documented and
its clear is a single register write. If the firmware needs one armed hardware
wake source on this board, this is it.

**With one open dependency, and it is not small.** The pull-up is only as
always-on as `+3V3`, and `+3V3` is **H8**. `docs/research/HARDWARE_MATRIX.md:205` — "Do not pick" the convenient reading —
says so about this exact rail, and the fact index carries the same warning:
`docs/research/VERIFIED_FACTS.md:1066` — "if the schematic is right". If
ALDO1 is the rail and #367's power owner ever gates it, R288 dies with it, IO17
floats or is dragged low, and an armed `GPIO_INTR_LOW_LEVEL` fires immediately
and forever — the failure this section prices for ALDO3 and touch at §3.4 ("and
its pull-up dies"), stated there because that rail's status is known. **No bench
step below takes `+3V3` down**: B3 reads IO17 with the PMU awake and B6 varies
only `ESP_PD_DOMAIN_RTC_PERIPH`. Arming this line is therefore gated on H8 as
well, and §5 says so.

**Line 2 — FT6336U INT to GPIO16, touch.**

| Row | Value | Evidence |
| --- | --- | --- |
| Polarity | Active LOW — the vendor drives it `INPUT_PULLUP`, and the repository already arms `GPIO_INTR_LOW_LEVEL` on the equivalent line | Vendor firmware and `firmware/main/board_power.cpp:286` — "esp_err_t result = gpio_wakeup_enable(touch_interrupt_, GPIO_INTR_LOW_LEVEL);" |
| External pull | **R551 10 kΩ to `AVDD`**, fed from `LDO3` | Schematic sheet 4 |
| Rail | ALDO3, shared with the display — `docs/research/HARDWARE_MATRIX.md:97` — "**separate I2C**: SDA 39, SCL 40, INT 16" | — |
| **The cost this imposes** | ALDO3 off means the FT6336U is unpowered **and its pull-up dies**, so IO16 floats or is dragged low and a LOW-level wake armed across that sleep fires immediately and forever. **Touch-as-wake requires ALDO3 to stay on through sleep** | Derived from schematic and rail map |
| Vendor precedent | Confirms exactly this: ALDO3 is disabled on sleep **only** when touch is not a wake source | Vendor firmware |
| No reset line | `docs/research/HARDWARE_MATRIX.md:222` — "pull-up R39 is `4K7/NC` — **not fitted**, no GPIO drives it" | — |
| Latch and clear semantics | **UNKNOWN**. The part has interrupt-trigger and polling modes selected by register `0xA4`, and the mode decides whether the line pulses or latches. What would settle it: the FocalTech FT6336U datasheet, `G_MODE` register 0xA4 | — |
| Behaviour on hardware | **NOT EXECUTED — HARDWARE REQUIRED** | — |

**This is the OD-20 price tag.** `docs/research/OWNER_DECISIONS.md:1604` — "## OD-20 — A10: wake the display on raise, button, or touch"
commits to touch as a wake path. On this board that decision costs ALDO3 held on
through sleep, and the current draw of a sleeping FT6336U plus panel is a number
nobody has measured here. The vendor's own comment offers roughly 103 µA for
screen plus touch, but that is `ESTIMATED` for our purposes — an upstream claim
rather than our measurement, and written about the display rail rather than
ALDO3.

**Line 3 — BMA423 INT1 to GPIO14, raise-to-wake.**

| Row | Value | Evidence |
| --- | --- | --- |
| Polarity | **Active HIGH** | Vendor firmware: `INPUT_PULLDOWN`, `RISING`, and the deep-sleep switch to `ESP_EXT1_WAKEUP_ANY_HIGH` |
| External pull | **None on the net** | Schematic sheet 4 |
| Drive mode | Must be push-pull, since an open-drain active-low interrupt on an unpulled net floats. The register values are UNKNOWN — see §3.3 | Derived |
| Wake mode | `GPIO_INTR_HIGH_LEVEL` for light sleep, `ANY_HIGH` for EXT1 | `esp_sleep.h` |
| **Composability** | **GPIO14 cannot share an EXT1 arming with GPIO16, 17 or 21.** The S3 has no per-pin EXT1 mode, and one line is HIGH while the others are LOW. Arming both groups requires the light-sleep GPIO path, which is per-pin — and that path has no wake attribution on the S3 | `soc_caps.h` and vendor firmware behaviour |
| Latch behaviour | **UNKNOWN**, and it decides whether the level wake spins | — |
| Features available | The vendor routes step counter, any-motion, no-motion, activity, tilt and wakeup all to INT1, so tilt and wrist-raise are available without INT2 | Vendor firmware |
| Behaviour on hardware | **NOT EXECUTED — HARDWARE REQUIRED** | — |

**Line 4 — AXP2101 IRQ to GPIO21, the power button.**

| Row | Value | Evidence |
| --- | --- | --- |
| Why it exists | The PWR key never reaches a GPIO — `docs/research/HARDWARE_MATRIX.md:113` — "**it never reaches a GPIO**, so every press arrives as a PMU interrupt" | — |
| Polarity | Active LOW, open-drain: an enabled event "will be pulled down" | AXP2101 §6.12.1 |
| Latched? | **Yes, and it is RW1C** — "Host will reset the IRQ status by writing '1' to status bit". Note this is the **opposite write polarity from the PCF8563**; two latched interrupts on one board with inverted clear conventions is a live footgun | AXP2101 §6.12.1, REG 48-4A |
| External pull | **R8226 47 kΩ to `VRTC` (net `1.8V`) plus R53 2 kΩ in series.** No 3.3 V pull-up | Schematic sheets 1-2 |
| Idle level at GPIO21 | About 1.8 V with the internal pull-up off, in the indeterminate band and possibly reading as asserted; about 2.58 V with the 45 kΩ internal pull-up on, roughly 0.1 V of margin on a typical-only spec | **ESTIMATED** from ESP32-S3 DS Table 5-4, p.65 |
| Required mitigation | Enable the internal pull-up, and for sleep specifically `rtc_gpio_pullup_en()`, exactly as the vendor does | Vendor firmware |
| Hard prohibition | **Never drive GPIO21 as an output.** The pin is bidirectional, and holding it low for more than 16 ms is a power-on request to the PMU | AXP2101 pin table and REG 26H[4] |
| **Shared-line hazard** | One line carries many events, and the REG 41 power-on defaults enable VBUS insert and remove, battery insert and remove, and both PWRON press lengths. Arming GPIO21 as "the button" therefore also wakes on plugging in a charger; button-only wake requires masking every other enable first, which is exactly what the vendor does before sleep and restores after | AXP2101 REG 41 defaults and vendor firmware |
| REG 40 and REG 42 defaults | **Not read.** Any enabled bit there also asserts the shared line. What would settle it: AXP2101 datasheet §6.13, the REG 40H and REG 42H default columns | UNKNOWN |
| Ownership | Every one of those register writes is a PMU write and belongs to the single power owner under ADR-0016 | Repository architecture |
| Behaviour on hardware | **NOT EXECUTED — HARDWARE REQUIRED** | — |

### 3.5 The one sentence the issue asked for

**ESP32-S3 GPIO wake from light sleep is level-triggered, not edge-triggered**,
and all four of these lines carry *latched* interrupts. A latched, open-drain,
active-low interrupt that nothing has cleared is still asserting when the wake
handler finishes, so re-entering sleep with that source armed wakes again
immediately and the device spins at full current instead of sleeping. On this
board, "clear the source's flag register before re-arming" is not hygiene; it is
the difference between a sleep and a busy loop. For the PCF8563 that clear is a
write to 01h with AF = 0 and TF = 1. For the AXP2101 it is a write of 1 to the
REG 48-4A status bits. For the FT6336U and the BMA423 the required clear is
`UNKNOWN`.

The corollary for the handler, given that the S3 has no
`esp_sleep_get_gpio_wakeup_status()`: on a GPIO light-sleep wake, read all four
pin levels first, then poll each device's status register, and treat
`esp_sleep_get_wakeup_cause()` as insufficient — use the bitmap form, because
simultaneous sources otherwise lose information.

### 3.6 A precedent already in this repository

`docs/research/HARDWARE_MATRIX.md:399` — "GPIO10 Light-sleep wake was physically disproved"
— and the corrected firmware wakes briefly every 100 ms to poll the latched PMU
edge instead, leaving the panel off on misses. A schematic-plausible wake line
failed on the bench there and was replaced by polling. **Nothing in section 3 is safe from
the same outcome**, which is why section 4 exists.

## 4. The bench procedure that would settle the UNKNOWNs

Physical unit `DC:B4:D9:18:49:40`, ordered, each step's precondition being the
previous step's pass. **Every step is `NOT EXECUTED — HARDWARE REQUIRED`.**
Re-verify the checksum of the existing factory backup before any flash.

**B0 — identify the board revision.** Photograph the silkscreen revision marking
and record it. Every result below is a claim about *that* revision. The physical
revision of this unit is currently UNKNOWN, and whether the `25-03-24` schematic
describes it is assumed rather than shown.

**B1 — opening register dump, before writing anything.** Read PCF8563 registers
0x00 through 0x0F in one burst at 0x51 and log all sixteen bytes verbatim. This
proves the part answers at 0x51, shows the state the factory image left, and
gives the value of **VL in 0x02** and **C in 0x07** on a unit whose coin cell has
been in place. `VL = 0` means the coin cell held the oscillator — record
`MEASURED`. A set C, combined with a plausible year in 0x08, reveals which
century convention the factory image used, settling §1.3 by measurement rather
than inference.

**B2 — VL round trip.** Read 0x02 and note VL; write a known time to 0x02-0x08
in one 7-byte burst with VL = 0; read back; power-cycle the system rail without
removing the coin cell and read again. This proves writes land, that VL clears by
interface, and that the coin cell backs the oscillator across a system power
cycle. Then, only with the owner's agreement, remove the coin cell for 60 s,
restore and read: VL should be 1. That is the only way to observe the set path,
and it is destructive to state.

**B3 — INT idle voltage on all four lines, PMU awake.** With the SoC pins as
high-impedance inputs, measure DC voltage on `IO17`, `IO16`, `IO14` and
`PMU_IRQ1`. Expect roughly 3.3 V on IO17, roughly 3.3 V on IO16 while ALDO3 is
up, whatever the BMA423 drives on IO14, and **about 1.8 V on PMU_IRQ1**. Then
enable the internal pull-up on GPIO21 and re-measure: **this is the measurement
that turns §3.4's 2.58 V from `ESTIMATED` into `MEASURED` and decides whether
GPIO21 is usable at all.** Below 2.475 V, the answer to "can the T-Watch wake on
the power button" is *not without an external pull-up* — a hardware limitation to
record, not a firmware bug to chase. Repeat the IO16 measurement with ALDO3
disabled to confirm the prediction that touch INT collapses with its rail.

**B4 — PCF8563 alarm, awake, no sleep.** Clear 01h; write 09h to the *next*
minute with `AE_M = 0` and 0Ah-0Ch to 0x80; set `AIE = 1`; poll 01h and the
GPIO17 level at 100 ms. Arm for the next minute boundary and never the current
one, per Figure 9. This proves AF sets, that INT goes low **and stays low**, and
bounds the delay. Then clear AF by writing 01h with **AF = 0, TF = 1, AIE = 1**
and confirm INT returns high with TF undisturbed — **the single most important
measurement in the plan**, because §3.5's whole re-arm story rests on it. Repeat
the clear as a read-modify-write and record it as "not exercised" rather than
"passed": D5 is a race, and a quiet bus does not disprove it.

**B5 — countdown timer, both TI_TP modes.** Write 0Eh = 0x82 (`TE = 1`,
`TD = 10`, 1 Hz) and 0Fh = 5. With `TIE = 1` and `TI_TP = 0`, observe INT low and
latched after about 5 s, then clear TF by writing 01h with **TF = 0, AF = 1** and
confirm INT releases. Set `TI_TP = 1` and scope the pulse: expect **1/64 s** on
the 1 Hz source. This proves D6 empirically as well as by reading, since
`TD = 10` is unreachable through the library, and the pulse width is what
justifies §1.7's refusal to use `TI_TP = 1` for wake.

**B6 — GPIO17 wake from light sleep.** Arm low-level wake, set a 60 s alarm per
B4, sleep, and log the wake time and the wake-cause bitmap. **Run B6, B7 and B8
twice: once with `ESP_PD_DOMAIN_RTC_PERIPH` held on and once without**, because
that domain decides whether internal pulls survive the sleep and the IDF header
says so explicitly — treat it as a variable, not a default. Then the decisive
negative test: wake, do **not** clear AF, re-arm and re-sleep immediately. Expect
an instant second wake; if it happens, §3.5 is `MEASURED`, and if it does not,
the latch is not behaving as the datasheet says and that is itself a finding.
Also arm the same pin through EXT1 and confirm the per-pin status bitmask
reports bit 17 — the only route to attribution on this SoC, and it should be
shown to work before anything depends on it.

**B7 — GPIO21 wake, button only.** Disable all PMU interrupts and clear REG
48-4A; enable only PWRON short press in REG 41; `rtc_gpio_pullup_en()`; arm
low-level wake; sleep; press PWR. This proves B3's mitigation suffices in the
sleeping domain. Then the shared-line test: repeat with the IRQ enables at their
power-on defaults and plug in USB — if that wakes the device, the §3.4 hazard is
`MEASURED` and the masking is mandatory rather than defensive. Read REG 48-4A on
wake and record which bit was set, the only per-event attribution this line has.

**B8 — GPIO14 wake, and the mode conflict.** Configure the BMA423 for tilt on
INT1, recording the exact register writes; measure the idle and asserted
voltages; arm `GPIO_INTR_HIGH_LEVEL` and light-sleep; move the wrist. This proves
the active-high polarity independently of the vendor firmware. Then the conflict
test: attempt an EXT1 arming of GPIO14 and GPIO17 together under `ANY_LOW` and
again under `ANY_HIGH`, recording which pin fails to fire, and then attempt
GPIO14 HIGH with GPIO17 LOW on the light-sleep GPIO path. If that works, the
light-sleep GPIO path is the only way to arm mixed-polarity sources on this SoC
and that is the design constraint. If it does not, **raise-to-wake and RTC-alarm
wake are mutually exclusive on this hardware** — a much larger finding, bearing
directly on OD-20. Read the BMA423 latch configuration back and record it,
closing §3.3's UNKNOWN by measurement if the datasheet stays unavailable.

**B9 — touch wake and its rail cost.** Arm GPIO16 low-level; sleep with ALDO3 on
and confirm touch wakes it; sleep with ALDO3 off and confirm the predicted
immediate wake or permanent assertion; then **measure sleep current at the
battery in both configurations**. That last number is the OD-20 figure, and the
vendor's 103 µA claim is about a different rail.

**B10 — sleep current baseline.** Measure battery current in light sleep with
nothing armed, with GPIO17 only, with GPIO17 and GPIO21, and with all four where
B8 permits. Record each as `MEASURED` with the meter and shunt used.

## 5. Proposed scope for the executable follow-up

This section is the proposal, deliberately kept inside the research report rather
than opened as a separate issue.

**In scope, in this order.**

1. **`firmware/main/pcf8563_time.h`** — a header-only, constexpr, I2C-free
   `decode_pcf8563` and `encode_pcf8563` pair structured exactly like
   `firmware/main/pcf85063_time.h` and reusing its `RtcDecodeStatus`. Base
   register **0x02**, seven bytes, VL from `raw[0] & 0x80`, century from
   `raw[5] & 0x80` with the convention chosen to match what B1 reads off the
   unit. Plus a constexpr helper that builds the 01h byte for "clear AF, preserve
   TF" and its mirror. Host tests table-driven, covering invalid BCD, VL set, the
   year-2000 boundary and both flag-clear directions.
2. **The T-Watch board file** gains the burst read at 0x51 and wires the result
   into `TimeService` with the same availability and validity reporting the
   Waveshare path already uses. No `#ifdef` in `core/` or `apps/`.
3. **The T-Watch wake arming** gains exactly **one** new source to begin with:
   the RTC alarm on GPIO17, the only line with a hard 3.3 V external pull-up, a
   documented latch and a one-write clear. The arming must be paired with a
   clear-before-re-arm in the wake path, or the mechanism is a busy loop — and
   it is gated on **H8**, because that pull-up is only as always-on as `+3V3`.
4. **`REUSE_LEDGER.md`** moves SensorLib's PCF8563 row from `EVALUATE` to
   rejected, citing section 2 — seven defects and no error propagation — and
   records that no licence obligation is incurred because nothing is taken.
5. **`HARDWARE_MATRIX.md`** gains the two schematic facts it lacks: R288 10 kΩ
   to `+3V3` on the RTC INT, carrying `+3V3`'s own **H8** status with it; and the
   AXP2101 IRQ's 47 kΩ pull-up to net `1.8V` with R53 2 kΩ in series and no
   3.3 V pull, the net's *voltage* being **H21**. It gains a third row for
   GPIO14 / BMA423 INT1 saying only what the schematic shows — **no external
   pull** — because the active level is a register the owning driver writes and
   is **H20**. Recording the vendor's configuration as a board fact is the
   mistake this repository already has on record —
   `docs/research/OPEN_QUESTIONS.md:132` — "A software choice had been promoted".

**Explicitly out of scope until the bench has run.** Arming GPIO21, GPIO16 or
GPIO14 as wake sources: B3 may show GPIO21 is unusable without an external part,
B8 may show GPIO14 and GPIO17 cannot be armed together, and B9 may show touch
wake costs more than OD-20 will pay. Building any of them first risks the GPIO10
outcome already recorded in §3.6. Also out of scope: any AXP2101 IRQ-enable
register write outside the ADR-0016 power owner, and any change to the PCF85063
path, because the two RTCs share a shape and not a register map.

**Gating.** Items 1, 2, 4 and 5 are desk-verifiable and can land now — item 1's
decode logic is fully determined by section 1 and testable on the host, and item
5 as reworded records only what the drawing shows. Item 3 needs **B1, B2, B4 and
B6** to pass on `DC:B4:D9:18:49:40`, **and H8 answered**, before it is more than
an untested arming call, and the pull request must say so. H8 is listed
separately because no bench step here takes `+3V3` down, so passing all four
would not discover it.

## 6. The other two candidates, the ADRs, and where this meets #367

### 6.1 XPowersLib 0.3.4 — evidence, not a dependency

The issue's warning about older pins is correct and now has its exact shape.
`lewisxhe/XPowersLib@d6997586` is dated 2026-07-01, its message is *"fix
axp2101 getIrqStatus byte order"*, and it touches exactly one file,
`src/XPowersAXP2101.hpp`. Any copy older than that returns a wrong IRQ word.

The three register groups the issue asked to locate, at that commit:

| What | Where | Registers |
| --- | --- | --- |
| Enable / mask | `enableIRQ()` `src/XPowersAXP2101.hpp:2640`, `disableIRQ()` `src/XPowersAXP2101.hpp:2651`, both into `setInterruptImpl()` `src/XPowersAXP2101.hpp:3096` | INTEN1–3, `0x40`–`0x42` |
| Status | `getIrqStatus()` `src/XPowersAXP2101.hpp:2590` | INTSTS1–3, `0x48`–`0x4A` |
| Clear | `clearIrqStatus()` `src/XPowersAXP2101.hpp:2602`, writing `0xFF` to each | write-1-to-clear, same three |

Two defects, both in the same class as SensorLib's D9 and both `VERIFIED` by
reading the source:

- **X1 — a failed read is reported as every interrupt pending.**
  `readRegister(uint8_t)` returns `int` and `-1` on failure
  (`src/XPowersCommon.hpp:284`, with the `return -1` paths below it), and
  `getIrqStatus()` assigns that straight into `uint8_t statusRegister[i]`. An
  I²C failure therefore becomes `0xFF` in all three bytes: indistinguishable
  from a genuine "everything fired". On this board that is the worst available
  direction to fail in — the wake handler would clear and re-arm sources that
  never fired and report a PWR key press that did not happen.
- **X2 — read-modify-write over an unchecked read.** `setInterruptImpl()` reads
  INTEN back at `src/XPowersAXP2101.hpp:3104` without checking it, then writes the modified value. On
  a bus error the mask is rewritten from `0xFF`. `clearIrqStatus()` returns
  `void` and ignores its writes' results.

Licence MIT. **Verdict: evidence, not a dependency.** What Attadipa needs from
this library is four register addresses and the knowledge that INTSTS is
write-1-to-clear; taking the library to obtain them imports X1 and X2 into the
one place ADR-0016 requires to be single-owner and to propagate failure. The
AXP2101 IRQ path is the power owner's work in any case, not this slice's.

### 6.2 LilyGoLib 0.2.0 — exact-board evidence, with one new reason to refuse it

Verified at `38e6f8d`:

| Finding | Evidence |
| --- | --- |
| Pins **XPowersLib 0.2.9** and **SensorLib 0.3.1** | `library.json` dependencies. 0.2.9 predates §6.1's byte-order fix by construction, so adopting LilyGoLib adopts a `getIrqStatus()` already known to be wrong |
| A missing PMU aborts the firmware | `src/LilyGoWatchS3.cpp:129` — `assert(0);` inside `if (!initPMU())` |
| One file-scope FreeRTOS event group is the whole interrupt plumbing | `src/LilyGoWatchS3.cpp:26` — `EventGroupHandle_t LilyGoWatch2022::_event;` |
| The touch bit is deliberately never cleared | `src/LilyGoWatchS3.cpp:387` — `// xEventGroupClearBits(_event, HW_IRQ_TOUCHPAD);  //No clear`, so `getTouched()` latches true for the life of the session |

The last row is the useful one, and it is evidence rather than a complaint: it
is exactly the behaviour §3.5 identifies as fatal for a light-sleep re-arm, and
the vendor ships it because its firmware never re-enters sleep under a held
touch line. **Verdict: evidence level 3 for pins, init order and interrupt
polarity. Not a dependency.**

### 6.3 Where board translation meets #367, and why it adds no second owner

Research question 6. The board contributes a *description of what the hardware
can do*, never a sleep path: which of the four lines exist, each one's polarity,
and the "clear this register, then verify the line is inactive" step that
belongs to each device. `#367`'s power owner stays the only caller of
`esp_light_sleep_start()` and the only writer of a PMU register, exactly as
ADR-0016 requires.

That keeps the board data out of `core/` and `apps/`: no `#ifdef BOARD_X`
appears, because the lines become data a board backend publishes and the owner
consumes. `wake_plan_is_legal()` is not weakened either — a source whose latch
this board cannot prove it can clear is simply never offered, which is a smaller
set of legal plans, not a larger one.

### 6.4 ADR change is not needed

Stated explicitly, as the issue asks:

- **ADR-0014 — time source, trust and synchronization.** The VL flag in §1.2 is
  a *source* of invalidity, which is what the ADR already models. An RTC that
  reports VL = 1 degrades to untrusted; nothing here asks the ADR to admit a new
  kind of trust.
- **ADR-0016 — one power owner.** §6.3. No rail is gated by another, no new
  `core::PowerState` appears, and no new task, queue or heap allocation is
  proposed.
- **ADR-0017 — a board backend composes ESP-IDF drivers; a vendor BSP is read,
  not linked.** §2.4, §6.1 and §6.2 reach the verdict this ADR already
  prescribes, three times over. They confirm it; they do not strain it.

### 6.5 The failure and simultaneous cases the implementation must carry

Research question 7. Each row is a required test, and none of them is executed:
**NOT EXECUTED — HARDWARE REQUIRED**.

| Case | Required behaviour | What proves it |
| --- | --- | --- |
| PWR + touch in the same wake | Both reported, neither lost | Two distinct events from one wake; both status registers read before either clear |
| RTC alarm + touch in the same wake | Both reported; AF cleared without clobbering TF (§1.5) | Register dump of 01h before and after the clear |
| A line already active on entry to sleep | Sleep is refused, or entered and the immediate re-wake is counted | Wake latency ≈ 0 and an event count that matches, not a silent spin |
| Held-low INT that nothing clears | The source is disarmed rather than re-armed | No more than one wake per held interval |
| I²C timeout on any of the four devices | Only that capability degrades; the others still wake | Injected timeout, then a successful wake from a different source |
| Corrupt RTC / VL = 1 | Time is reported invalid, never as trusted (ADR-0014) | `TimeService` state after a backup-battery removal |
| First boot with no NVS metadata | Same as VL = 1, and recovery is possible | Erase, boot, set, reboot |
| Brownout during an RTC write | The stored time is either the old or the new value, never a torn BCD | Read-back after an induced brownout |
| Event queue full | Oldest-dropped or newest-dropped, stated and counted, never silent | Event counter across a burst |
| Partial board init (missing BMA, RTC or PMU) | The board comes up with that capability absent; the panel still restores | Each device disconnected in turn |

The pass criteria are the issue's own, restated with no invented numbers: no
lost or duplicated input across the fixed cycle set, no immediate re-wake after
a clear, no stale armed source, RTC invalidity never reported as trusted time,
and a partial failure that degrades only its own capability.

## Open UNKNOWNs

| Unknown | What would close it |
| --- | --- |
| FT6336U interrupt mode — pulse or latched, and how it is cleared | FocalTech FT6336U datasheet, `G_MODE` register 0xA4 and the interrupt section |
| BMA423 INT1 output driver mode, active level and latch registers | Bosch BMA423 datasheet, `INT1_IO_CTRL (0x53)` and `INT_LATCH (0x55)`. The SensorLib source would be evidence level 2, not the datasheet |
| The configured output voltage of `VRTC` on this unit — the datasheet allows 1.8, 2.5, 3 or 3.3 V, set by EFUSE/OTP with no documented user register | Bench step **B3**. No document answers this for a specific unit |
| AXP2101 REG 40H and REG 42H power-on IRQ-enable defaults | AXP2101 datasheet V1.4 §6.13, the default columns |
| The PCB revision of `DC:B4:D9:18:49:40`, and whether the `25-03-24` schematic describes it | Bench step **B0** |
| Whether this unit's coin cell holds the oscillator across a power cycle | Bench step **B2** |
