# Who owns power on this watch

Research for [#292](https://github.com/hleserg/Attadipa/issues/292). No
production code changes here: this document decides a contract, and the
executable work it justifies is scoped in §8.

The question is not "how do we save battery". It is narrower and it has a
deadline: the shipping firmware now has a second consumer of the hardware —
the MeshCore BLE transport — and a third, GNSS, is next in the platform
audit. Today exactly one function may put the SoC to sleep and exactly one
function may write a PMU rail, and neither knows the other exists. That
arrangement is correct only for as long as there is one consumer. This
document establishes what the seam between product policy and board backend
must guarantee before the second consumer is allowed to have an opinion.

## 0. What is established, and how

| Claim | Standing |
| --- | --- |
| The Light-sleep transaction lives in the physical-input path and nowhere else | **VERIFIED** — read in this tree, §2.1 |
| Exactly one function writes AXP2101 rail registers | **VERIFIED** — read in this tree, §2.2 |
| The MeshCore BLE transport contains no power, sleep or PM code at all | **VERIFIED** — read in this tree, §2.3 |
| The GPIO wake source armed before sleep is never disarmed anywhere in the tree | **VERIFIED** — read in this tree, §2.4 |
| The firmware reads one wake cause, not the bitmap, and infers touch by re-reading the pin | **VERIFIED** — read in this tree, §2.4 |
| `esp_pm_lock` is recursive and is not thread-safe per handle | **VERIFIED** — ESP-IDF v5.5.5, §3.1 |
| `esp_sleep_get_wakeup_causes()` exists at the pinned IDF and returns a bitmap | **VERIFIED** — ESP-IDF v5.5.5, §3.1 |
| Zephyr restores the usage count when a suspend or resume callback fails | **VERIFIED** — Zephyr v4.4.2, §3.2 |
| Zephyr resumes exactly the devices it recorded suspending, in reverse order | **VERIFIED** — Zephyr v4.4.2, §3.2 |
| XPowersLib builds against ESP-IDF's `driver/i2c_master.h`, not only Arduino | **VERIFIED** — XPowersLib `d699758`, §3.3 |
| The IRQ byte-order bug that pin exists for cannot reach this firmware today | **VERIFIED** — §3.3 |
| Which loads sit on each Waveshare AXP2101 rail | **VERIFIED** — D13, resolved 2026-08-28, §6.1 |
| Current draw per power state, on either board | **NOT EXECUTED — HARDWARE REQUIRED** — H1 |
| Whether the AXP2101 on these boards can measure current at all | **NO** — H2, answered 2026-09-05 from both AXP2101 datasheets: the ADC has five channels and every one is a voltage or a temperature. Whether either *board* fits a sense resistor is the half still open |
| Which wake sources are usable in practice and what each costs | **NOT EXECUTED — HARDWARE REQUIRED** — H5 |
| AMOLED brightness against power | **NOT EXECUTED — HARDWARE REQUIRED** — H6 |
| Whether BLE survives Light-sleep on this board, and at what cost | **NOT EXECUTED — HARDWARE REQUIRED** — §4.6 |
| What the six idle LDO rails are actually costing | **UNKNOWN** — nothing was measured, §6.1 |

Nothing in this document was simulated. Every source claim was read at a
pinned revision; every code claim was read in this tree at `main`.

## 1. Three of the issue's premises have moved

The issue was written against `main@9f07c06`. Three of the facts it builds on
are no longer the facts, and one of them is the stated blocker. This section
exists because inheriting a stale premise is how research produces a confident
wrong answer.

**The blocker is gone.** The issue says *"Blocker: D13 rail-to-load mapping не
позволяет безопасно проектировать production rail gating"*. D13 was resolved on
2026-08-28 by [#313](https://github.com/hleserg/Attadipa/issues/313), reading
the schematic as a drawing:
[`OPEN_QUESTIONS.md:131`](OPEN_QUESTIONS.md) — "**ALDO1** (3.3 V) = net". The
rail-to-load map is in §6.1 below. Rail gating is no longer blocked on
evidence; it is blocked on nobody owning it, which is this document's subject.

**Sleep has already moved out of `WatchControl`.** The issue describes
`watch_control.cpp` as performing the whole Light-sleep transaction. It no
longer does. The transaction is in the physical-input path, and `WatchControl`
now learns that a sleep happened by watching a counter change:
[`firmware/main/watch_control.cpp:252`](../../firmware/main/watch_control.cpp) —
"Sleep belongs to the physical path now, and it does not call into here.". That
is a better arrangement than the one the issue describes, and it is also the
first instance of the exact pattern this document is about: a consumer
discovering a power transition after the fact rather than participating in it.

**PR #282 shipped.** The issue says the Mesh runtime must not be treated as
shipping. It is shipping — the BLE transport is in `main` — so research
question 8 stops being a question about a draft and becomes a reading of
source. The answer is in §2.3, and it is more interesting than the issue
expected.

Two premises survive intact and are re-confirmed below: `esp_pm_lock`'s
recursiveness and per-handle thread-safety caveat (§3.1), and the XPowersLib
`getIrqStatus` byte-order boundary at `d699758` (§3.3).

## 2. What the shipping path actually does

> **Sections 2 and 3 read the tree as it was at `144459f`, before the owner
> this research produced existed.** [ADR-0016](../adr/0016-one-power-owner.md)
> was accepted from it and then implemented, which moved most of the code cited
> below into `firmware/main/board_power.cpp`. The citations follow the text to
> where it lives now, so that they stay checkable rather than rotting into a
> line number that lands on something real and unrelated; where the finding no
> longer describes the current code, the paragraph says so. What the sections
> record is why the owner was built, and that does not change.

### 2.1 One sleep transaction, in the input loop

`maybe_sleep()` is the whole of it:
[`firmware/main/physical_input.cpp:170`](../../firmware/main/physical_input.cpp) —
"void maybe_sleep() {". It is the only caller of `esp_light_sleep_start()` in
the tree — now
[`firmware/main/board_power.cpp:417`](../../firmware/main/board_power.cpp) —
"const esp_err_t result = esp_light_sleep_start();" — and the only caller of any
`esp_sleep_enable_*`.

Its shape is already close to the transaction the issue asks for, and saying so
matters: this is not a codebase that needs to be told what a power transaction
is. It refuses on a busy input queue or a held button; it builds a wake plan and
validates it against the product model before touching hardware —
[`firmware/main/physical_input.cpp:222`](../../firmware/main/physical_input.cpp) —
"plan.state = attadipa::core::PowerState::LightSleep;"; it arms wake sources;
it takes the AMOLED down; it sleeps in a loop that re-arms the PMU poll timer;
it restores the panel and republishes the UI.

What it does not have is a way for anyone else to take part.

### 2.2 One rail writer, which is accidentally right

`initialize_pmu()` programs three rails and enables two:
[`firmware/main/board_power.cpp:597`](../../firmware/main/board_power.cpp) —
"DC1 3.3 V", then ALDO1 and
[`firmware/main/board_power.cpp:599`](../../firmware/main/board_power.cpp) —
"ALDO2 3.3 V", enabling them read-modify-write at
[`firmware/main/board_power.cpp:606`](../../firmware/main/board_power.cpp) —
"ESP_RETURN_ON_ERROR(write_reg(pmu, 0x90, aldo | 0x03), kTag,". Its comment
states the discipline it is keeping —
[`firmware/main/board_power.cpp:595`](../../firmware/main/board_power.cpp) —
"// Preserve unrelated rails. The known-working board implementation needs".
The three writes moved into the owner unchanged; `initialize_pmu()` now calls
`board_power_bring_up_rails()` and the boot sequence is byte-identical.

Two things follow from D13 that this code could not have known when it was
written.

It is right about ALDO2 for the wrong reason. ALDO2 is not a supply: it is a
10 K pull-up holding `DSI_PWR_EN` high (§6.1). Enabling it is load-bearing for
the display, and the code enables it because the vendor implementation did.
Nothing in the tree records *why* it must never be switched off, so the next
person to write a power manager and see an "unused 3.3 V rail" will switch it
off and get a fault that reads as broken wiring.

It never touches ALDO3, ALDO4, BLDO1, BLDO2, CPUSLDO, DLDO1 or DLDO2, and the
factory register state has all nine LDOs on. ALDO3 feeds the vibration motor
through a transistor gated from GPIO18, so the rail being live is harmless. The
other six feed nothing. How much they cost is **UNKNOWN** — nothing has been
measured — but they are the clearest candidate for the first thing a power
owner would be allowed to do.

### 2.3 The Mesh consumer has no power opinion whatsoever

*It has one now. #367 item 7 gave the node link a lease, and the boundary this
section said could be drawn once is drawn:*
`core/include/attadipa/core/node_link_lease.h:119` — "class NodeLinkLease {".
*The paragraph below is what it was, and the reasoning it records is why the
declaration is recorded on the sleeper's task rather than in the transport's.*

Research question 8 asks where the ownership boundary with the BLE/Mesh
consumer runs. Read in this tree at `main`, `firmware/main/meshcore_ble.cpp`
contains no occurrence of `sleep`, `power`, `esp_pm` or any PM lock, in any
case. Not a constraint, not an intent, not a lock — nothing.

That is not a defect today and it must not be filed as one. It is the
significant fact for this design: the boundary the issue expected to have to
*adjudicate* does not exist yet, so it can be *drawn*, once, before either side
has grown a habit. Concretely, two things are true at the same time and neither
is checked by anything:

- `maybe_sleep()` will enter Light-sleep while a BLE connection is up and
  possibly mid-frame, because nothing tells it not to; and
- the BLE transport will keep running its state machine across that sleep, or
  not, and nobody knows which, because it has never been measured on this
  board — **NOT EXECUTED — HARDWARE REQUIRED**.

The upstream evidence attached to #292 is exactly about the second half of
that sentence going wrong. The Meshtastic finding (PR #11650, merged `f8a8d12`)
is a transport being resurrected by its own asynchronous callback *inside* an
in-progress shutdown; the Offband finding (PR #1015) is a capability probe
driving a shared rail low and leaving the radio's supply floating. Both are the
same shape: a consumer acting on the hardware while a system transition is in
flight, because the transition was not visible to it.

### 2.4 Two concrete gaps the current code already has

Neither is a live defect. Both are the seam failing quietly, and both are the
strongest available argument for the contract in §4 — because they are what
"one owner, informally" degrades into on its own.

**The armed wake plan is never reconciled with hardware.** *Fixed by the owner;
this is what it was.* Before sleeping, the code armed a GPIO wake — the call is
now [`firmware/main/board_power.cpp:322`](../../firmware/main/board_power.cpp) —
"esp_err_t result = gpio_wakeup_enable(touch_interrupt_, GPIO_INTR_LOW_LEVEL);",
reached only from `arm_wake()` and journaled. On the way out it disarmed exactly
one source — [`firmware/main/board_power.cpp:353`](../../firmware/main/board_power.cpp) —
"result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);", which is now
one arm of a `disarm_wake()` that the transaction calls for each source it
recorded — and
`esp_sleep_disable_wakeup_source` appears nowhere else in the tree. The GPIO
wake configuration therefore persists across every cycle.

Disarming a source twice is not an error the board may pass on, and that is a
fact about ESP-IDF rather than a preference: every branch of
`esp_sleep_disable_wakeup_source` is guarded by `CHECK_SOURCE(source, value,
mask)`, defined at `components/esp_hw_support/sleep_modes.c` as
`((s_config.wakeup_triggers & mask) && (source == value))`, so a source whose
trigger bit is already clear falls through to the function's `else` and returns
`ESP_ERR_INVALID_STATE` (v5.5.5, read locally). Light sleep never clears those
bits — only this function does — so the first disarm after a sleep is a real
one and it is the owner's recovery retry that arrives second. `disarm_wake()`
therefore reports `ESP_ERR_INVALID_STATE` as the postcondition it is: the source
is not armed. Reporting it as a failure would make a board that is provably in
the requested state unrecoverable. The persistence noted at the end of the paragraph above is harmless while
every sleep entry arms the same plan, and it stops being harmless the moment a
second consumer arms a different source: `wake_plan_is_legal()` will have
validated a plan the SoC does not hold, because the SoC holds the union of
every plan ever armed. The product model at
[`core/include/attadipa/core/power_state.h:75`](../../core/include/attadipa/core/power_state.h) —
"bool wake_plan_is_legal(PowerState state, std::uint16_t armed);" is a
statement of intent that nothing reconciles against the hardware.

The same paragraph applies to the two early-return failure paths — an arm
failure, and a panel-off failure. Both return with wake sources already armed
and `sleep_requested_` already cleared. Nothing entered sleep and nothing
disarmed. That is a partial transaction with no rollback, in the one place the
firmware already treats as a transaction.

**One wake cause is read where a bitmap is available.** *Fixed by the owner.*
The code read `esp_sleep_get_wakeup_cause()` and then decided touch by
re-reading the pin. It now reads the bitmap —
[`firmware/main/board_power.cpp:430`](../../firmware/main/board_power.cpp) —
"const std::uint32_t soc = esp_sleep_get_wakeup_causes();" — and the pin is a
corroborating signal that only logs a warning:
[`firmware/main/board_power.cpp:441`](../../firmware/main/board_power.cpp) —
"gpio_get_level(touch_interrupt_) != 0) {". ESP-IDF's own header says of the
single-cause API: *"This API will only return one wakeup source. If multiple
wakeup sources wake up at the same time, the wakeup source information may be
lost."* (`components/esp_hw_support/include/esp_sleep.h` at v5.5.5, lines
716–717). The bitmap alternative `esp_sleep_get_wakeup_causes()` exists at the
pinned revision (same file, line 728) — the issue asserted this and it checks
out. Re-reading the pin is a workable substitute for exactly one GPIO source
and stops being one at two, and it is a race besides: a touch that has already
lifted by the time the level is read is classified as the timer.

This is the sharpest instance of the general problem. The wake classification is
correct today because there is one plausible wake per source; it is not correct
by construction.

### 2.5 Startup is a transaction — with a rollback since #367 item 6

Recorded here because it is the same defect on the other end of the lifetime,
and the executable issue in §8 should not fix one without the other. Until
#367, `start_waveshare_ui()` raised I2C, then the PMU, then display, then
touch, every step `ESP_RETURN_ON_ERROR`: a touch failure after a successful
display returned with the panel up, LVGL running, the PMU programmed and
nothing torn down — a partially initialised board reported as a failure.

The owner contract's `prepare → commit → rollback` shape is the same shape
boot needs, and boot has it now. Each required-step failure calls
[`firmware/main/waveshare_board.cpp:1301`](../../firmware/main/waveshare_board.cpp) —
"esp_err_t abandon_board() {", which reads the journal from `BoardState`'s
handles. Before an LVGL display exists, it releases every completed step in
reverse. After a display exists, the display stack is deliberately retained;
the rest of the journal still rolls back. RTC and touch failures are reported
and boot continues: the clock shows unavailable, the input service runs
without a touch controller and never names a touch wake
([`firmware/main/physical_input.cpp:228`](../../firmware/main/physical_input.cpp) —
"A boot that got no touch controller"), and the face says `no touch`.

Two things the rollback leaves behind, on purpose. The rails, always: the
bring-up wrote them, and switching any of them off is authorised by a
measurement nobody has made (ADR-0016; ALDO2 is the `DSI_PWR_EN` pull-up, not
a supply), so they stay as written and the log says so:
[`firmware/main/waveshare_board.cpp:1291`](../../firmware/main/waveshare_board.cpp) —
"rails stay as written". And the whole display stack — LVGL, the display, the
panel, its IO and the QSPI host — whenever rollback cannot prove that a queued
transfer has completed. The LVGL mutex serialises API calls; it is not a QSPI
DMA completion barrier. The pinned `esp_lvgl_port` is `2.8.0~1`, component
hash `fb6c1fdf…5d75da4`, and its own manifest binds it to
`espressif/esp-bsp@d14ff131`. Its complete public display header exports four
operations — three `lvgl_port_add_disp*` variants and
[`lvgl_port_remove_disp()`](https://github.com/espressif/esp-bsp/blob/d14ff131266bf1392efff88db72cb4638897507c/components/esp_lvgl_port/include/esp_lvgl_port_disp.h#L132-L139),
whose last declaration is `esp_err_t lvgl_port_remove_disp(lv_display_t *disp);`.
There is no wait, timeout or transfer-semaphore operation in that public API;
the header read on 2026-09-03 has SHA-256
`0938ac0f248c03bac10427baebe0c674a90eed90c64769d835d9947fadd73227`.

There is consequently no release-with-display branch or caller-provided
quiescence claim: a registered display is always retained.

**Two boards, one question, two answers, and the difference is not style.**
The decision is written once, board-agnostic and template-only, in
[`firmware/main/boot_rollback.h:34`](../../firmware/main/boot_rollback.h) —
"void rollback_boot_retaining_all(Ops &ops) {" for the T-Watch and
[`firmware/main/boot_rollback.h:55`](../../firmware/main/boot_rollback.h) —
"void rollback_boot_retaining_display(Ops &ops) {" for the Waveshare, and
`tests/test_boot_rollback.cpp` pins every branch of both off a board. The DMA
argument above reaches the display stack and nothing else: it authorises
retaining neither board's I2C. What separates the answers is who owns the
touch handle. On the T-Watch the LVGL port does —
[`firmware/main/twatch_board.cpp:727`](../../firmware/main/twatch_board.cpp) —
"state.indev = lvgl_port_add_touch(&touch);" — and rollback removes no indev,
so an LVGL still running still reads that `esp_lcd_touch_t`; retaining the
display has to retain touch and its bus with it. On the Waveshare the input
service owns its own `lv_indev_t` and deletes it on its own failure
([`firmware/main/physical_input.cpp:93`](../../firmware/main/physical_input.cpp) —
"lv_indev_t *indev = lv_indev_create();" — created there and released at
[`firmware/main/physical_input.cpp:107`](../../firmware/main/physical_input.cpp) — "lv_indev_delete(indev);"),
so nothing LVGL keeps points at the touch controller and only the display stack
is retained.
That path is reached both when boot's LVGL lock times out
([`firmware/main/waveshare_board.cpp:1365`](../../firmware/main/waveshare_board.cpp) —
"return abandon_board_after(ESP_ERR_TIMEOUT,") and when
physical-input startup fails after `create_ui()` may have queued the first frame
([`firmware/main/waveshare_board.cpp:1428`](../../firmware/main/waveshare_board.cpp) —
"return abandon_board_after(physical_result,").
Freeing the panel or host while its DMA callback is pending would be a
use-after-free. On the physical-input failure, everything `create_ui()` armed
is disarmed while the caller still owns the LVGL lock — five things, not two:
[`firmware/main/waveshare_board.cpp:1413`](../../firmware/main/waveshare_board.cpp) —
"lv_obj_remove_event_cb(lv_screen_active(), long_press);" — removes the path
into provisioning/RTC,
[`firmware/main/waveshare_board.cpp:1415`](../../firmware/main/waveshare_board.cpp) —
"lv_obj_remove_event_cb(lv_screen_active(), node_page_turn);" — removes the
node page turn, the adjacent timer deletion removes `refresh_ui()`, and
[`firmware/main/waveshare_board.cpp:1421`](../../firmware/main/waveshare_board.cpp) —
"state.clock_face.clear();" — takes the two the clock face installs on the same
screen. The second of those is the one that matters, and it is a power defect
rather than a tidiness one: the retain branch skips `lvgl_port_deinit()`, so a
surviving [`ui/lvgl/clock_face.cpp:177`](../../ui/lvgl/clock_face.cpp) —
"motion_timer_ = lv_timer_create(motion_tick, kMotionPeriodMs, this);" would
invalidate and flush QSPI every
[`ui/lvgl/clock_face.cpp:11`](../../ui/lvgl/clock_face.cpp) —
"constexpr std::uint32_t kMotionPeriodMs = 50;" for the life of a boot that has
no path into Light-sleep, behind a panel this path leaves dark. Its own pause at
[`ui/lvgl/clock_face.cpp:179`](../../ui/lvgl/clock_face.cpp) —
"lv_timer_pause(motion_timer_);" does not save it: that is for a theme without
fireflies, and this board asks for Night
([`firmware/main/waveshare_board.cpp:1012`](../../firmware/main/waveshare_board.cpp) —
"{kWidth, kHeight, attadipa::ui::Theme::Night,"). The current cost is
**ESTIMATED** from the period; nothing has been measured on a board.
The board power adapter is detached before its PMU handle and I2C bus are
released ([`firmware/main/waveshare_board.cpp:1279`](../../firmware/main/waveshare_board.cpp) —
"attadipa::firmware::board_power_detach();"), so the retained panel cannot
leave the owner paired with a dangling PMU handle. The retained UI stays
allocated and is left with nothing armed on it — no callback, no timer, no
animation — and `BoardState` keeps its live display handles rather than
reporting them absent.

That leak is unbounded — the LVGL task and its buffers stay allocated for the
life of the boot — and it is the trade made: a leak is recoverable and a hang
or a panic is not. **NOT EXECUTED — HARDWARE REQUIRED:** every path through
`abandon_board()` is a boot-failure path; none has been provoked on a board,
and whether a CO5300 whose `esp_lcd_panel_del` failed leaves the QSPI bus
freeable is UNKNOWN.

## 3. What the three upstream candidates actually give

### 3.1 ESP-IDF v5.5.5 — `USE AS-IS`, and it is not a power manager

Verified in the local pinned tree (`git describe` = `v5.5.5`, commit
`b774170ff46`).

`esp_pm_lock_acquire` is recursive: *"The lock is recursive, in the sense that
if esp_pm_lock_acquire is called a number of times, esp_pm_lock_release has to
be called the same number of times in order to release the lock."*
(`components/esp_pm/include/esp_pm.h`, lines 123–125). It may be called from an
ISR, and it carries the caveat the issue quoted: *"This function is not
thread-safe w.r.t. calls to other esp_pm_lock_* functions for the same
handle."* (same file, lines 129–130). Read precisely, that caveat is narrower
than it looks — the hazard is concurrent operations on *one* handle, not on the
subsystem — which means the natural Attadipa mapping is **one handle per
consumer**, never a handle shared between them.

The three lock types are `ESP_PM_CPU_FREQ_MAX`, `ESP_PM_APB_FREQ_MAX` and
`ESP_PM_NO_LIGHT_SLEEP` (same file, lines 47–57). That enumeration is the
whole of what the SoC layer can be told. It cannot be told that the display is
still draining a frame, that a Mesh acknowledgement is outstanding, or that
ALDO2 must stay up. The rejection in the issue stands, and it stands for a
sharper reason than "it does not know the board": the lock vocabulary has no
term for an external device at all.

`esp_sleep_get_wakeup_causes()` is present and returns a bitmap
(`components/esp_hw_support/include/esp_sleep.h`, line 728). Adopt it.

**Verdict: `USE AS-IS`.** These are the pinned SDK's public APIs and the
physically exercised path. Nothing is wrapped, nothing is reimplemented. What is
added above them is the part they decline to have an opinion about.

### 3.2 Zephyr v4.4.2 — `INSPIRE ARCHITECTURE`, four invariants, no code

Verified by reading `subsys/pm/device_runtime.c`, `subsys/pm/pm.c` and
`subsys/pm/device_system_managed.c` at tag `v4.4.2`
(`671f64aa79924606253238f801c494c84b02c2a0`), Apache-2.0. Four invariants are
worth taking, and they are worth taking as *tests*, not as a device model.

**A failed action does not move the count.** On a synchronous suspend whose
callback fails, `device_runtime.c` puts the usage count back before returning
(`pm->base.usage++` immediately after `action_cb(... SUSPEND)` returns
negative, lines 100–103). Resume is symmetric: a failed `RESUME` decrements the
count it had incremented and releases the domain it had claimed (lines
288–296). The consequence is the invariant: **a consumer that asked and was
refused holds nothing.** Attadipa's current code violates the analogue of this
in §2.4 — a failed arm leaves the SoC armed.

**A release below zero is an error, not a wrap.** `if (pm->base.usage == 0U)`
→ `LOG_WRN("Unbalanced suspend")` → `-EALREADY` (lines 77–81). Double-release
is detected, not absorbed.

**Only what was suspended is resumed, and in reverse.** `pm_suspend_devices()`
records each device it actually suspended into a slot array and counts them;
`pm_resume_devices()` walks `num_susp - 1` down to zero
(`device_system_managed.c`, lines 63–64 and 70–82). This is a transition
journal, and it is the direct answer to the Zephyr regression the owner
attached to this issue on 2026-08-31 (`a9775e14`): a domain that suspended a
child outside the journal was not resumed by the system resume, and the fix was
to record who suspended whom.

**A failed system suspend un-does itself.** `subsys/pm/pm.c` line 201:
`if (!pm_suspend_devices()) { pm_resume_devices(); ... return false; }`. The
system does not enter a low-power state it could only half prepare for.

**Verdict: `INSPIRE ARCHITECTURE`.** The rejection of a wholesale port stands
and needs no re-argument: the implementation is welded to Zephyr's device
model, devicetree power domains, work queues and Kconfig, and importing it puts
a second device framework on top of ESP-IDF. What transfers is four sentences
and a test matrix.

### 3.3 XPowersLib at `d699758` — `REJECT` for now, on a narrower ground than the issue's

Verified against the pinned commit
(`d6997586e68f65afd51baa775903df930db39821`, 2026-07-01, MIT). Two of the
issue's stated concerns do not survive contact with the source, and the verdict
is still `REJECT` — which is worth writing down, because a rejection resting on
a wrong reason gets overturned by the first person who checks.

**It is not Arduino-only.** `XPowersAXP2101.hpp` guards its Arduino include
(`#if defined(ARDUINO)` … `#else #include <math.h>`, lines 31–35), and
`XPowersCommon.hpp` has a first-class ESP-IDF path that selects
`driver/i2c_master.h` for IDF ≥ 5.0 under `CONFIG_XPOWERS_ESP_IDF_NEW_API`
(lines 36–51). That is the same new-style I2C API `waveshare_board.cpp` already
uses. The integration cost is lower than the issue assumed.

**The 99 KB header is not a 99 KB cost.** `XPowersAXP2101.hpp` is 3142 lines of
in-class inline definitions. Member functions defined in a class body that
nobody calls are never emitted. The flash cost is roughly the API surface
actually used, not the file.

**The byte-order bug cannot reach this firmware.** The pin exists because
`getIrqStatus()` assembles three status bytes into one word (lines 2590–2596),
and earlier revisions got the order wrong. Attadipa never assembles that word:
it reads register `0x49` as a single byte and masks it —
[`firmware/main/board_power.cpp:473`](../../firmware/main/board_power.cpp) —
"const esp_err_t read_result = read_reg(pmu_, kAxpInterruptStatus2, &status);" against
the mask in `firmware/main/power_button_edges.h`. The known bug is real and the
pin is right, and neither is currently load-bearing here.

So why still `REJECT`. Because of what the library is *for*. Its API surface is
76 `enable…`/`disable…`/`set…` methods, and its design premise is that every
rail is an independently switchable thing exposed to whoever holds the object.
On this board `disableALDO2()` is a call that blanks the display through a route
that looks like a wiring fault (§6.1), and `disableALDO3()` is one that kills
the vibration motor's supply. Adding a dependency whose whole shape contradicts
the invariant this document exists to establish — that a rail has exactly one
owner and a recorded reason — moves the hazard inside a component the reviewer
does not read.

The current driver is three register writes and two reads
(§2.2). It is not a driver that needs replacing; it is a driver that needs a
rail-graph in front of it. That graph is Attadipa-specific and is the actual
deliverable, and once it exists the question "XPowersLib or three more
`write_reg` calls" becomes a small, reversible implementation detail behind it.

**Verdict: `REJECT` for the current scope; re-open as `WRAP` if and when the
charger/gauge API is needed.** Charging termination is a live question — D22
established this cell terminates at 4.35 V, `MEASURED` — and if the executable
work grows a battery-state consumer, XPowersLib's charger and gauge coverage is
the strongest reason to reconsider, behind the same wrapper, at the same pin.

### 3.4 The comparison, in one table

| | ESP-IDF v5.5.5 | Zephyr v4.4.2 | XPowersLib `d699758` | Attadipa today |
| --- | --- | --- | --- | --- |
| Knows SoC frequency and sleep | yes | via its own port | no | via ESP-IDF |
| Knows external rails | no | via devicetree domains | yes, generically | 3 registers, no model |
| Knows rail→load wiring | no | yes, per board | **no** | only in prose, D13 |
| Reference-counted consumers | per lock handle | per device, usage count | no | **none** |
| Ordered transaction | no | yes, journalled | no | implicit, one caller |
| Rollback on failure | n/a | yes, symmetric | no | **partial, §2.4** |
| Publishes availability | no | device state | no | `core::PowerState` |
| Licence vs GPL-3.0-or-later | Apache-2.0, compatible | Apache-2.0, compatible | MIT, compatible | — |
| Verdict | `USE AS-IS` | `INSPIRE ARCHITECTURE` | `REJECT` (revisit as `WRAP`) | extend |

Licence compatibility above is the direction only — Apache-2.0 and MIT into
GPL-3.0-or-later. The mechanics of copying, linking and redistribution notices
live in [*Where the resolved graph lives, and where notices go*](DEPENDENCIES.md#where-the-resolved-graph-lives-and-where-notices-go)
and are deliberately not duplicated here.

## 4. The contract

One paragraph of recommendation per research question. The design goal is the
smallest thing that makes §2.4 impossible, not a power framework.

### 4.1 What the seam is (RQ1)

A **power owner** — one object, on the board side — with three obligations and
nothing else: it is the only code that may write a rail or enter sleep; it
performs each transition as prepare → commit → rollback; and it publishes the
resulting `core::PowerState` and per-resource availability. Consumers do not
call it to do things. They hold **leases** describing what they need to remain
true, and the owner decides.

Nothing here is a dependency-injection framework, a device model or a driver
abstraction. Three types: a resource id, a lease, and a transition record.

### 4.2 Who may do what (RQ2)

| Action | Permitted to |
| --- | --- |
| Write AXP2101 enable or voltage registers | the power owner, only |
| Turn the AMOLED off/on, set brightness | the power owner, only |
| Arm or disarm an ESP32 wake source | the power owner, only |
| Call `esp_light_sleep_start` / `esp_deep_sleep_start` | the power owner, only |
| Publish hardware/service `Availability` | the power owner, only |
| Declare a need (CPU, radio up, display up, wake source) | any consumer, as a lease |

Enforce it mechanically, not in prose: the executable issue's acceptance should
include a source check that `esp_light_sleep_start`, `esp_sleep_enable_*` and
any write to AXP2101 registers `0x80`, `0x90`, `0x82`, `0x92`–`0x95` appear in
exactly one translation unit. That check is cheap and it is the only thing that
survives a reviewer's attention lapsing.

### 4.3 Leases (RQ3)

Reference-counted, fixed-capacity, no heap. A lease is a small struct in a
statically sized array indexed by consumer, holding a resource bitmask and a
deadline. Take Zephyr's three invariants verbatim (§3.2): a refused acquire
grants nothing; a release below zero is a reported error rather than a wrap;
and the count is restored when the underlying action fails.

Leaked and over-released leases are found without a framework: the array is
fixed and enumerable, so the owner can log every held lease with its holder and
its age on each transition refusal, and a lease past its deadline is reported —
not silently reclaimed. Silently reclaiming is how a consumer ends up believing
it holds hardware it does not, which is the Meshtastic failure in §2.3 with the
polarity reversed.

Deadlines are a diagnostic, not a scheduler. There is no new task and no timer
thread: the owner already runs on the input loop that calls `maybe_sleep()`.

### 4.4 Keeping the product model separate (RQ4)

`core::PowerState` stays exactly as it is and gains nothing. It is the product
vocabulary — states, legal transitions, the wake-source whitelist, provenance —
and it must remain buildable and testable on a host with no ESP-IDF. The owner
lives on the board side and *translates*: product `WakeSource::Touch` becomes a
GPIO number and a level; product `PowerState::LightSleep` becomes a set of
released `esp_pm_lock`s and a rail plan. The rail graph is board data and lives
with the board, never in `core/` and never behind an `#ifdef`.

The one addition `core` needs is the ability to state a plan and have it
checked: today `wake_plan_is_legal()` is asked and the answer is discarded if
hardware disagrees. §4.5 closes that.

### 4.5 The transaction, and what happens when rollback fails (RQ5)

```
prepare   collect leases; refuse if any consumer denies; build the wake plan
validate  wake_plan_is_legal() AND read back what the SoC actually has armed
suspend   consumers in dependency order, recording each one that succeeded
rails     apply the rail plan, one rail at a time, against the rail graph
sleep     esp_light_sleep_start()
classify  esp_sleep_get_wakeup_causes() — the bitmap, not the single cause
resume    exactly the recorded consumers, exact reverse order
publish   PowerState and per-resource availability
```

Failure at any step un-does exactly the steps that succeeded, using the record
rather than re-deriving them — Zephyr's journal (§3.2), and the direct lesson of
the `a9775e14` regression the owner attached to this issue.

The ordering constraint the codec evidence establishes (`uhrwerk-rs` `1986e3f`,
corrected by `11437824e0`) is that a device's own low-power command must be
issued **while its clock and bus are still up**, and its postcondition must be
read back. Generalised: quiesce the consumer, issue the device command, verify,
*then* stop the bus, *then* gate the rail. "We called every shutdown callback"
is not a postcondition. This is the reason `rails` comes after `suspend` in the
sequence above and not before.

If rollback itself fails, the owner must not paper over it. The rule is
Zephyr's `TURN_ON_FAILED` flag: the affected resource is published `Failed`,
not `Ready`, and any consumer whose lease depends on it is refused until a
successful re-initialisation. **Unpowered hardware must never be reported
Active.** A failed rollback is a defect worth a reboot, and the honest state is
what makes that decision possible.

### 4.6 Deep-sleep is a reboot (RQ6)

It is not a resume and must not share the resume path. Nothing in RAM survives.
The design consequence is small and worth stating so it is not discovered later:
what must cross the boundary is what is already crossing it — RTC wall time,
already proven on hardware — plus, if a future consumer needs it, an explicit
record in RTC-retained memory or NVS, written before entry and validated on the
way up. Deep-sleep is out of scope for the first executable issue: the shipping
path has never entered it, and adding an untested reboot boundary to a change
that also introduces the owner is two risks in one diff.

### 4.7 Wake configuration is state, and it must be reconciled (RQ7)

Established in §2.4: wake configuration persists across cycles and this
firmware disarms exactly one source. The rule is that the owner disarms
everything it armed, on every exit path including every failure path, and then
validates by reading back rather than by trusting its own bookkeeping. Use
`esp_sleep_get_wakeup_causes()` and classify from the bitmap; keep the pin
re-read only as a corroborating signal, never as the classifier.

### 4.8 The Mesh boundary (RQ8)

*Shipped in #367 item 7, and not in this shape. Three differences, because this
section is what an agent reads to learn the layout: the transport does not hold
the lease — it publishes a phase and the sleeping task records the declaration,
which is decision item 2 of [ADR-0016](../adr/0016-one-power-owner.md); the
lease is not confined to a live connection, because `Attached` is a radio
running an unbounded active scan and holds it too
(`core/include/attadipa/core/node_link_lease.h:95` — "return phase == TransportPhase::Attached");
and `maybe_sleep()` still decides, because nothing gates a rail on `NodeLink`
yet. The paragraph below is the design as proposed.*

The BLE transport declares a lease and never chooses sleep. Concretely: while a
connection is up, or a send is in flight, it holds a lease naming the radio and
`NO_LIGHT_SLEEP`; when idle it holds nothing. `maybe_sleep()` stops being a
function that decides and becomes a function that *asks*.

What is genuinely `UNKNOWN` and must not be designed around: whether NimBLE
survives Light-sleep on this board, what a connection costs across it, and
whether a peer's reconnect is observable after a wake — **NOT EXECUTED —
HARDWARE REQUIRED**. The contract does not need those answers, because a lease
is an assertion by the consumer, not a prediction by the owner. The rail and
current *policy* does need them, and it comes second.

### 4.9 The GNSS boundary (RQ9)

`core::gnss_power.h` already models cold/warm/hot start and retention, and it
already knows that support may be unestablished. The owner can promise a GNSS
provider exactly two things before any provider exists: a lease that keeps a
named rail up across a state the provider chooses, and a truthful
`Availability` when it cannot. It must promise nothing about retention across
Deep-sleep, because the module and its rail on either board revision are
`UNKNOWN` and a promise made now would be a guess wearing a contract's clothes.

### 4.10 D13 and the probe plan (RQ10)

Closed without a probe. §6.

## 5. The rejected alternatives, re-checked

Each of the issue's pre-written rejections was re-verified against the sources
above rather than inherited. All six hold; two hold for different reasons than
the issue gave.

| Alternative | Verdict | Reason, as it stands now |
| --- | --- | --- |
| Every driver enables its own rail | **REJECT** | unchanged in force, but no longer because the mapping is unknown — because it is now known and shows shared, mislabelled rails: ALDO2 looks free and holds the display up (§6.1) |
| `esp_pm_lock` as the whole power manager | **REJECT** | confirmed at the source: the lock vocabulary is three SoC frequency/sleep terms and has no term for an external device (§3.1) |
| Import Zephyr's PM/device model | **REJECT** | unchanged: a second device framework over ESP-IDF. Four invariants and a test matrix transfer; the implementation does not (§3.2) |
| Import XPowersLib unpinned and unaudited | **REJECT** | still right, and the audit is now done: the Arduino and size objections do not survive it, the API-shape objection is what carries the rejection (§3.3) |
| Automatic Light-sleep replacing the explicit transaction | **REJECT** | the current path is physically proven and needs explicit AMOLED, touch and PMU choreography that automatic entry cannot express |
| A universal power/device framework for all future boards | **REJECT** | `PLATFORM_AUDIT`'s standing rule: a minimal seam with real consumers, not speculative abstraction. §4 is three types |

## 6. Hardware evidence

### 6.1 D13, resolved — the rail map

From [#313](https://github.com/hleserg/Attadipa/issues/313), 2026-08-28, by
reading the schematic as a drawing, with the register decode traced to AXP2101
Datasheet V1.4 §6.13.2.75–77 rather than to library source. Recorded at
[`OPEN_QUESTIONS.md:131`](OPEN_QUESTIONS.md) — "**ALDO1** (3.3 V) = net"; not
restated as a new fact here, only as the input this contract consumes.

| Rail | What it is | May the owner gate it? |
| --- | --- | --- |
| ALDO1, 3.3 V | net `A3V3`, analogue audio supply, via `0R` links R17/R29/R34 | yes, when audio holds no lease |
| ALDO2 | **not a supply** — an R10 10 K pull-up on `DSI_PWR_EN`, no decoupling | ⚠️ **never**. The panel's `VCI`/`VDDIO` come from `VCC3V3` and no GPIO drives `DSI_PWR_EN`; switching ALDO2 off blanks the display by a route that reads as a wiring fault |
| ALDO3, 3.0 V | vibration motor, R7 `0R` → P1, switched by Q1 MMBT3904 from GPIO18 | yes; the motor is already gated at the transistor, so this is a second-order saving |
| ALDO4, 1.8 V | feeds nothing | yes |
| BLDO1, BLDO2, CPUSLDO, DLDO1, DLDO2 | feed nothing | yes |
| DC1, 3.3 V | the main supply the board is brought up on | no |

`LDO_ON_OFF0 = 0xFF` and `LDO_ON_OFF1 = 0x01`: all nine LDOs are on from the
factory. Six of them feed nothing. **What that costs is UNKNOWN — nothing was
measured**, and the saving must not be estimated from the datasheet's quiescent
figures and then quoted as a result.

The ⚠️ row is the single most important line in this document for anyone
writing the executable change. It belongs in a board-side table where a wrong
call fails a build or a test, not in prose — the same argument
[`OPEN_QUESTIONS.md:126`](OPEN_QUESTIONS.md) — "must never be driven as an output"
makes for GPIO 6.

### 6.2 The probe plan, withdrawn

The issue asked for a reversible AXP2101 rail probe if the schematic did not
close D13. The schematic closed it. **No probe is needed and none should be
run**: writing PMU rails on a board to learn what is already established from a
drawing is risk with no information return. If some future rail question does
need a probe, the constraints stated in the issue remain binding — save every
touched register, one rail at a time, restore exact bytes immediately, no
eFuse, no secret, no irreversible setting, and never ALDO2.

### 6.3 What is still owed to hardware

`H1` current per state, `H5` usable wake sources and their cost, `H6` AMOLED
brightness against power — all **UNKNOWN**, all listed in `OPEN_QUESTIONS.md`,
none changed by this document. `H2` is now **PARTIAL**: the PMU cannot measure
current at all, so those three need a board shunt or external instrumentation
rather than a register read. Add to them: BLE behaviour across Light-sleep (§4.8)
and the cost of the six idle rails (§6.1).

None of these block the contract, and that is the point of separating them. The
contract decides *who* may act and *in what order*; the measurements decide
*what policy is worth applying*. Building the first while the second is
outstanding is correct. Shipping a rail-gating policy on vendor quiescent
figures would not be.

## 7. The test matrix this justifies

Designed here, implemented in the executable issue. Everything except the last
group runs on a host build with no hardware.

**Leases.** Two consumers holding one resource; acquire/release balance;
release below zero reported, not wrapped; a refused acquire grants nothing; a
lease past its deadline reported and not reclaimed; the fixed array exhausted.
And, since #367's reproduction: a handle from a slot's first generation is
still `NotHeld` when that slot is at its last one; a slot whose generations are
spent is retired rather than wrapped, so no two handles in a table's lifetime
are equal; a table with every slot spent answers `Retired` with no count moved,
which is not the recoverable `Exhausted` of a full one.

**Transaction.** Dependency order observed; a consumer refusing at prepare
aborts before any hardware is touched; suspend failure at step *n* rolls back
exactly steps 1..*n*-1 in reverse; resume failure publishes `Failed` and not
`Ready`; rollback failure leaves the resource `Failed` and refuses dependent
leases; a shared rail is not gated while any lease names it.

**Wake.** An illegal wake plan is refused; a stale wake configuration is
detected by read-back and not merely by bookkeeping; simultaneous causes are
both reported from the bitmap; an unknown cause is `UNKNOWN` and not silently
attributed to the timer; every failure path disarms what it armed.

And, since #367's P3 finding: a cause the *board* derives survives a
simultaneous SoC cause too. The row above was written about the bitmap and so
only covered the SoC's half, which is exactly the half that was never in doubt;
the board's half is worse, because the AXP2101's latch is write-one-to-clear —
[`docs/research/TWATCH_RTC_INPUT_WAKE.md:508`](TWATCH_RTC_INPUT_WAKE.md) —
"| Latched? | **Yes, and it is RW1C**", traced there to AXP2101 §6.12.1,
REG 48-4A — and the read that proves a press is the read that spends it. So the matrix asks
what the classification reports **and** whether it touched the register at all:
a route that reads `0x49` and then reports no `Button` has not lost a cause, it
has deleted one, and the next descent cannot find it either. One classification
operation answers every route —
[`firmware/main/wake_classification.h:79-80`](../../firmware/main/wake_classification.h) —
"template <typename ConsumePowerEdge>" — because two branches reading one
destructive register for different purposes is what the finding was.

**States.** `Active → Idle → LightSleep → Idle → Active`; a failed entry leaves
the SoC in `Active` with nothing armed; capability and service availability
track rail `Off` / `Ready` / `Failed` and the state after a rollback.

**Ownership, statically.** `esp_light_sleep_start`, `esp_sleep_enable_*` and
AXP2101 rail-register writes each appear in exactly one translation unit. USB
watch-control, BLE/Mesh and any future GNSS provider contain none of them.

**HIL — every item below is `NOT EXECUTED — HARDWARE REQUIRED` until it runs on
a board.** Repeated touch and PWR wake; panel restore; RTC retention; BLE
reconnect and a message round-trip across a sleep cycle; reset reason; current
per state; the six idle rails gated and the display, touch, audio, SD, RTC and
IMU each still answering afterwards.

## 8. Scope for the executable issue

One finite issue, filed as #367. It is deliberately smaller than this
document.

**In scope.** A board-side power owner holding the rail graph from §6.1 —
including ALDO2's ⚠️ as data, not prose. A fixed-capacity lease table with
Zephyr's three invariants. `maybe_sleep()` moved behind it, unchanged in
behaviour, so the first change is a seam and not a policy. The bitmap wake
classification. Disarming every armed source on every exit path. Rollback for
the boot sequence in §2.5. The BLE transport declaring a lease. The static
ownership check. The host-side tests in §7.

**Out of scope, explicitly.** Any rail actually being gated to save power —
that needs H1/H2 and a measurement, and the seam has to exist before a policy
can be judged. Deep-sleep (§4.6). Any GNSS provider. XPowersLib. Charging
policy. Audio codec teardown, which is real (`uhrwerk-rs`, §4.5) and belongs to
the issue that brings up audio.

**Definition of done.** No behaviour change observable on the bench except the
wake classification, which becomes correct where it was previously
coincidental; the static ownership check passing; §7's host tests passing; and
the HIL rows recorded as `NOT EXECUTED — HARDWARE REQUIRED` until a board runs
them.

## 9. Sources

| Source | Revision | Licence | Read for |
| --- | --- | --- | --- |
| ESP-IDF | `v5.5.5`, `b774170ff46`, local clone | Apache-2.0 | `esp_pm.h` lock semantics; `esp_sleep.h` wake API and the single-cause warning |
| Zephyr | `v4.4.2`, `671f64aa79924606253238f801c494c84b02c2a0` | Apache-2.0 | `subsys/pm/device_runtime.c`, `subsys/pm/pm.c`, `subsys/pm/device_system_managed.c` — usage-count and journal invariants |
| Zephyr regression | `a9775e14`, tests `3f921af8` | Apache-2.0 | the domain-suspended-child bug behind the journal invariant |
| XPowersLib | `d6997586e68f65afd51baa775903df930db39821`, 2026-07-01 | MIT | `XPowersAXP2101.hpp`, `XPowersCommon.hpp` — ESP-IDF support, API shape, `getIrqStatus` byte order |
| Meshtastic firmware | PR #11650, merged `f8a8d1247786fe19f19cd07dce75e702d9d463f7` | GPL-3.0 | transport resurrected inside an in-progress shutdown |
| Offband meshcore-firmware | PR #1015 (open), commits `8fad89a1`, `c524beec` | MIT | capability probe driving a shared rail; unreachable low-voltage cutoff |
| `agrucza/uhrwerk-rs` | `1986e3f`, `11437824e0`, `afb3fadb34` | GPL-3.0 | codec teardown ordering and verified postcondition, on this same board family |
| AXP2101 Datasheet | V1.4 §6.13.2.75–77 | vendor | the rail register decode behind §6.1 |
| Waveshare schematic | `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf` | vendor | the rail-to-load map, read as a drawing |

Upstream evidence marked GPL-3.0 above was read for its invariants and its
failure modes. No code from any of it is copied by this document or by the
scope in §8.
