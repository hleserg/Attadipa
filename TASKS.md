# Tasks

Sections are states, not folders. A task moves; it is not copied.

| Section | Meaning |
|---|---|
| `NOW` | actively being worked on — at most a couple of items |
| `NEXT` | chosen, starts as soon as NOW clears |
| `READY` | dependencies known, research done, could start today |
| `BLOCKED` | cannot proceed — must carry the blocker record |
| `WAITING` | waiting on someone or something external |
| `DONE` | finished and verified |

`READY` means **genuinely ready**. If the critical research is not done, it is
not READY — it is a research task. Priorities are `P0`–`P3`.

Every task carries: priority · dependencies · goal · acceptance criteria ·
research status · implementation status · tests · hardware required.

**Task state is updated in the same commit as the change it describes**
(final §73). A status file several commits behind is not a status file.

## This file and the GitHub issue queue

They are not the same list and neither is a copy of the other.

| | Holds | Lifetime |
|---|---|---|
| **this file** | the roadmap — milestones, dependencies between large pieces of work, and the record of what was decided and why | a task here is days of work and outlives any one agent run |
| **GitHub issues** | executable work packages, findings, bugs, research assignments | an issue is one agent run, or a few |

The link is by reference only: an issue that implements part of a task here
names it (`T-045`), and a task here that has been split into issues names their
numbers. Nobody maintains two copies of the same sentence, because a copy goes
stale silently. The protocol is
[`docs/automation/AI_TASK_PROTOCOL.md`](docs/automation/AI_TASK_PROTOCOL.md).

---

## NOW

### T-100 · The agent queue, verified by running it rather than by reading it
- **Renumbered from T-054 on 2026-08-22, and do not renumber it back.** Two
  different pieces of work carried that ID: this one, and the transport tests
  in the `DONE` section, which commit `5810e20` names as T-054 in its message.
  History keeps the number it was recorded under; the live task takes a fresh
  one. `python3 tools/docs/check_docs.py` fails if this ever happens again.
- **Priority:** P1
- **Dependencies:** none — the automation is merged on `main`
- **Goal:** the loop closes without the owner as transport: a finding becomes an
  issue, an issue becomes a branch and a draft pull request, CI and an
  independent reviewer act on it, and a stranded task is recovered without
  anybody noticing it was stranded.
- **Acceptance:** a producer files an issue and an agent starts on it with no
  copy/paste; a refused task says so on the issue rather than only in a run log;
  a stale `reviewed_head` causes the finding to be re-verified rather than
  implemented; the kill switch stops Anthropic spending while ordinary CI keeps
  running.
- **Research status:** done. Routine capabilities checked against the published
  documentation before design, and two constraints changed it: routine GitHub
  triggers support **Pull request and Release only, not Issues** — so intake
  must live in `claude-agent.yml` and cannot be a routine — and a routine's API
  trigger carries a bearer token but no actor, which is the wrong shape for a
  gate whose entire security model is the actor's write access.
- **Implementation status:** live. Six workflows, an hourly watchdog, and a
  daily backstop routine scoped to what a workflow cannot detect about itself.
- **Tests:** `actionlint` over six workflows with shellcheck integration —
  clean; `shellcheck -x` over both scripts — clean; intake gate, 16 hostile
  cases — 16/16; host build 10/10; simulator 12/12, both geometries. Production:
  smoke test A ([#5](https://github.com/hleserg/Attadipa/issues/5)) exercised
  intake, marker-derived labels, the `@claude` dedup override and a green Claude
  run, and exposed the stuck-label defect now fixed.
- **Hardware required:** no.
- **Not verified by execution:** the no-credential BLOCKED path (a credential is
  configured, so that step is skipped rather than run), and the producer-identity
  path — see the open question below.
- **Open inside this task:** how ChatGPT actually authenticates when it files an
  issue. If it posts through a GitHub App its login ends in `[bot]` and the gate
  rejects it, correctly and by design. A user account with `write` or better is
  required. Until an issue has actually been filed that way, this is `UNKNOWN`
  and it is the single thing standing between the queue and the owner being
  removed from the loop.


### T-110 · The mandated reading list is 500 KB before the agent opens a file
- **Priority:** P2
- **Dependencies:** none. Spun out of T-107, which measured this while looking
  for a cause and then found a different one.
- **Goal:** decide whether the reading order the agent prompt mandates is
  affordable, on its own merits rather than as a suspect.
- **What is measured, and it is only a measurement:**

  | File | Size |
  |---|---|
  | `TASKS.md` | 149 KB |
  | `docs/research/REUSE_LEDGER.md` | 69 KB |
  | `STATUS.md` | 63 KB |
  | `docs/research/OWNER_DECISIONS.md` | 62 KB |
  | `docs/master-prompt-final.md` | 62 KB |
  | `.github/workflows/claude-agent.yml` | 54 KB |
  | the rest of the mandated order | 47 KB |

  Over 500 KB — roughly 140k tokens — before the agent opens a file the task is
  actually about, and an automation task then opens the workflows on top of it.
- **This is no longer evidence of anything.** T-107 treated it as the likely
  cause of the unexplained agent deaths. It was not: the cause was
  `allowed_bots: ""` refusing the watchdog's dispatcher before the model was
  ever reached. The sizes stayed true and stopped being a clue, which is the
  ordinary fate of a plausible theory and worth leaving written down.
- **Acceptance:** either a measured statement that the reading order fits with
  room to work — from a real run's own token accounting, not an estimate — or a
  restructuring that makes it fit. An agent is never permitted to skip
  `CLAUDE.md` or the specification; if something gives, it is the *form* of the
  reading, such as a summary the prompt points at, not the obligation.
- **Do not** trim a document or reorder the prompt to chase a number. The last
  time this file recorded a hunch acted on early, `--max-turns` went to 40 and
  cost six runs in an afternoon.
- **Tests:** `.github/tests/*.sh` stay green, and any change is observed on a
  real run of a real issue rather than asserted.
- **Hardware required:** no.


### T-108 · An `unclassified` agent failure is a gap in a whitelist, and it should not stay one
- **Priority:** P2
- **Dependencies:** none. T-107 is **done** and its cause turned out not to be
  a model failure at all, so the two are no longer the same investigation.
- **Goal:** keep [`failure-reason.sh`](.github/scripts/failure-reason.sh)
  answering. Its whitelist covers the failures this project has actually had —
  an API status line, a context refusal, a credit balance, an expired OAuth
  token, a service `overloaded_error`. Anything else is reported as
  `unclassified`, honestly and uselessly.
- **Acceptance:** when a failure comment says `unclassified`, the pattern that
  would have named it is added with a test case, and the run that motivated it
  is cited in the test the way every other case there is. The whitelist is the
  security model — the same log holds every tool result — so a pattern is added
  by shape, anchored and length-bounded, and never by widening one until
  something matches.
- **Tests:** `.github/tests/failure-reason-test.sh`, which must keep its
  leak cases: an API key, a token-shaped string and a private key sitting in the
  log beside a real error, asserting only the error comes out.
- **Hardware required:** no.


## NEXT

### T-034a · The mascot, at a size somebody drew
- **Priority:** P2, and it is **an owner decision before it is work.**
- **Dependencies:** T-034 (**done**)
- **Goal:** get one mascot pose into `ui/assets/source/` at a size the pipeline
  will accept. `docs/ui/reference/lumar_mascot_sheet.png` supplies four named
  poses and DESIGN_SYSTEM §7 already maps them to states, but the sheet is a
  1440-pixel desktop concept drawing and the pipeline refuses it — correctly.
- **The question, and it is not an agent's to answer:** at `image.size.hero`
  (120 dp — 196 px on the T-Watch, 236 px on the Waveshare) a pose lifted from
  the sheet is roughly a 2× reduction, which is arguably the *"derived and
  cleaned artwork"* path `docs/ui/reference/README.md` describes. At icon sizes
  it is not arguable at all: 40 px of a 450-pixel drawing is noise, and §86
  forbids it outright. So: **derive at hero size, or redraw?** The owner should
  decide looking at pixels, not at this paragraph.
- **Acceptance:** either a committed source asset with its provenance recorded,
  or a written decision that the mascot is redrawn and by whom. Not a scaled
  crop committed quietly.
- **Hardware required:** no. **Owner required:** yes.

### T-037 · The first Clock
- **Priority:** P0
- **Dependencies:** T-008, T-009, T-033, T-034
- **Goal:** the first real screen. Time, date, battery, a good watchface, day and
  night, EN and RU, a Child variant, and one purposeful use of the owner's art
  (final §58, §88).
- **Acceptance:** it looks like Attadipa and not like debug UI (final §96), at
  both geometries, in both locales, in both themes.
- **Research status:** not started — mature wearable watchface patterns
  (final §85); interaction lessons only, never someone else's visual identity
- **Implementation status:** not started
- **Tests:** reference screenshots across the visual matrix
- **Hardware required:** no

### T-038 · The first Settings
- **Priority:** P0
- **Dependencies:** T-037, T-017 (ADR-0006, **done**)
- **Goal:** language, theme, brightness, sound, haptics, power profile, Child
  Mode, diagnostics (final §88). Language comes first in the list, because a
  user who cannot read the settings screen cannot change the language from it.
- **Acceptance:** every control is backed by `SettingsService`, not by local
  state; values validated on write **and read**; a Russian layout that survives
  longer strings.
- **Research status:** decided in [ADR-0006](docs/adr/0006-settings-and-bounded-values.md)
- **Implementation status:** not started
- **Tests:** round-trip persistence; out-of-range rejection; layout at both
  geometries in both locales
- **Hardware required:** no

---

## READY

### T-062 · The branch audit's remaining findings
- **Priority:** P1. These are defects in code that already exists and that other
  work is about to build on, which makes them cheaper now than later.
- **Dependencies:** none
- **Goal:** close, or consciously decline with reasons, each finding below.
  Every one of them was read in the source before being written here; none is a
  report taken on trust. Five from the same audit are already fixed —
  `d2bf02c` (the CRC did not cover the last byte), `f46578c` (three in the trust
  evaluator), `7e4c4f9` (the replay rig could not produce Stale) and issue #26
  (the movement/altitude baseline, below) — and the rule from the research
  prompt applies: **do not stop after the first fix.**

- **A state that cannot say "nobody has checked".** `GnssCapabilities`
  (`core/include/attadipa/core/gnss_power.h:51`) is four plain `bool`s defaulting
  to `false`, so "this receiver has no backup domain" and "nobody has read the
  datasheet yet" are the same value. T-051 and T-052 exist precisely because
  those answers are not yet known, and the type cannot hold the state the
  project is actually in. Compare `ReceiverIndication`, which gets this right
  with `Unknown` and `Unsupported` as distinct values, and OD-5, which is the
  decision saying they must be.

- **A default-constructed snapshot claims to be trusted.** `GnssStatus::trust`
  (`core/include/attadipa/core/diagnostics.h:116`) defaults to
  `TrustState::Trusted`. A snapshot nobody filled in therefore reports the most
  reassuring answer available. `validity` on the line above defaults to `NoFix`,
  which is the right instinct; `trust` should be `Untrusted` for the same
  reason, or the field should be optional so that "not evaluated" is sayable.

- **Rates for a relayed fix were divided by the wrong interval — fixed,
  issue #26.** `TrustEvaluator::observe` used to set `previous_position_at_
  = now` rather than `observation.observed_at`, and to advance the baseline
  for any observation carrying a coordinate regardless of `PositionValidity`.
  For a receiver on the board `now` and `observed_at` are equal so this never
  showed there; for a position relayed by an Attadipa node over a link that
  queues and retries they are not, and the interval was measured from
  *arrival* rather than measurement, overstating the implied speed — the same
  shape as the bug `f46578c` fixed, by a different door. A `NoFix` sample that
  retains the last coordinate on the wire had the same effect by pulling the
  baseline timestamp forward without the position actually moving. Both the
  position and altitude baselines now advance only on a `Valid` or `Degraded`
  observation whose `observed_at` is not older than what is already stored;
  an equal timestamp is measured as zero elapsed time and an out-of-order one
  is evaluated but not adopted as the new baseline. Regression tests in
  `tests/test_trust.cpp`. **Review of that fix (PR #71) found it traded the
  arrival-time bug for a future-dated one: nothing bounded `observed_at`
  against `now`, so a single sample dated far ahead was `in_order`, seeded
  the baseline with a near-zero implied speed, and froze both rate detectors
  for the rest of the session — closed the same call by treating a
  future-dated `observed_at` exactly like an out-of-order one** (bounded by
  `TrustPolicy::observed_at_forward_skew`, 50 ms, reusing `ClockDisagreement`
  rather than adding a reason for the same condition). Poisoning-sequence
  regression test added; confirmed red against the pre-review code.

- **`holds()` and `reasons()` can report evidence that has expired.** Both read
  `live_`, which only `update()` prunes, and both are `const`. A caller that
  reads without having called `update(now)` first gets past-TTL evidence and has
  no way to tell. Either the readers take `now`, or the class documents that a
  reader must update first and something enforces it.

- **Zero means two things in the transport.** `Decoder::next()` returns 0 for
  "nothing ready" and for "a zero-length frame was delivered", and
  `FrameQueue::pop()` has the same ambiguity. A zero-length frame is legal —
  `encode()` accepts it and the round-trip tests cover it — so a caller
  draining until zero silently drops one.

- **`Attach` while `Faulted` reports the wrong refusal.** It returns `Redundant`
  where `Ignored` is the truth: nothing about a faulted link makes a new attach
  redundant, and the two words tell an operator different things.

- **`Detach` hardcodes `PeerClosed`.** A detach the *device* initiated is
  recorded as one the peer initiated. That is the field-report evidence for the
  most common question about a node link — who let go first.

- **Recovery can complete with no observation at all.** The clean-hold window
  can elapse while nothing whatever has been reported, so silence promotes the
  state. OD-5's rule is that silence is not an all-clear; this is the one place
  the code still treats it as one.

- **`FixLost` and `StalePosition` both weigh 20 against a `degrade_at` of 30.**
  So neither, alone, moves trust. That may be the intended two-axis design —
  validity already carries the whole freshness story, and trust is about whether
  numbers can be believed rather than when they were taken — and
  `02-fix-goes-stale.trace` now asserts the current behaviour either way. What
  is missing is the decision, written down, rather than a number nobody chose.

- **Resync costs a full CRC per candidate offset.** Correct and O(n·m) on a
  noisy link. Worth measuring before optimising, and worth a note either way:
  the frame is at most 192 bytes and the link is slow, so this may be entirely
  affordable. `ESTIMATED`, not measured.

- **Acceptance:** each item either fixed with a test that fails without the fix
  — mutation-verified, as the four already closed were — or declined in writing
  with the reason. A silent decline is not one.
- **Research status:** n/a
- **Implementation status:** not started
- **Tests:** host, per item
- **Hardware required:** no, except the resync measurement, which is a HIL note
  rather than a HIL plan.

### T-063 · The cheapest way to find a lost watch, costed before the expensive ones
- **Priority:** P1 — it is the alternative the owner has not been shown, and it
  is cheaper than every option in
  [TAGS_TRACKS_RECKONING](docs/research/TAGS_TRACKS_RECKONING.md) §1
- **Dependencies:** [COMPANION_PROTOCOL](docs/mobile/COMPANION_PROTOCOL.md)
- **Goal:** establish what "the companion phone remembers where it last saw the
  watch over BLE, and the watch remembers the phone" actually delivers, and what
  it costs, so the owner can compare it against emulating somebody else's tag
  network rather than being offered only the expensive answer.
- **Why it is first:** it needs no Apple ID, no MFi membership, no Google
  proposal form, no Samsung partnership, no reverse-engineered protocol, no
  other company's SIG identifier and no server. It is also the only variant that
  works with the companion this repository has already specified.
- **What to answer:** what fraction of "I lost it" is answered by a last-seen
  position and a timestamp; what a phone can record in the background on each
  platform without being killed; whether the reverse direction (the watch
  remembers the phone) is useful or noise; and the honest failure mode — the
  watch left somewhere the phone never went.
- **Acceptance:** a written comparison against §1.2's Apple route on the same
  axes — coverage, latency, cost, who has to trust whom — with the recommendation
  stated either way.
- **Research status:** not started
- **Implementation status:** not started — no code comes out of this task
- **Tests:** none. It produces a research record.
- **Hardware required:** no.

### T-065 · `track/`: recording, storage, and a simplifier that fits
- **Priority:** P2 — **sized, unblocked**, by
  [OD-13](docs/research/OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature) §2
- **Dependencies:** T-046 (crash-safe persistence), T-047 (two clocks), T-067,
  and now T-060/T-061 — see the recording rule below
- **Goal:** a core library that records a track and survives the application
  being closed, the device sleeping, and a flat battery — because a breadcrumb
  trail that stops when the screen does is not one.
- **THE RECORDING RULE IS NOT A TIMER, and this is what the owner answered.** A
  track exists for a walk somebody may have to retrace on foot. So: the watch
  learns **familiar ground** — places where the wearer stays a long time while
  moving only locally; inside it nothing is recorded; past a threshold beyond
  its edge, **on foot**, recording starts; on return the track is **erased**.
  Vehicles are out of scope — that is what a phone is for. Background recording
  is configurable and **on by default**.
- **What that rule costs, named rather than discovered later:**
  - **"on foot" needs motion-mode recognition**, or the watch records in a car,
    which is the one case that was excluded. It rests on the pedometer, which
    exists only as OD-6 — so this task cannot ship ahead of T-060/T-061;
  - **"familiar ground" is learned anchors**, i.e. stored personal history. That
    is a privacy surface and it belongs to T-069, in Child Mode especially;
  - **threshold, hysteresis and dwell are three numbers that do not exist.**
    Propose them with arithmetic. Too small records every trip to the shop; too
    large starts recording once it is already too late;
  - the **upper bound is now a walk**: order of a couple of hours, single-digit
    kilometres, hundreds to a few thousand points — not the multi-day route §3
    sized against. Recompute the encoding budget from that, do not inherit it.
- **What has to be decided rather than assumed:**
  - **every point carries its source and its uncertainty.** A GNSS point, a
    point reckoned from an anchor and a point with no anchor at all are three
    different things, and merging them into "a track" is the same lie as a
    confident arrow with no heading. A reckoned point is a first-class point
    with a radius, never a bare coordinate;
  - **the simplifier is online.** Douglas-Peucker needs the whole track in
    memory — 28.8–43.2 kB at 3600 points — on a device that is recording
    continuously. Zhao-Saalfeld sleeve-fitting is linear and explicitly does not
    need all the data at once, and being online means the same routine decimates
    while recording rather than only before sending;
  - **coordinate resolution and the sampling rule are one decision**, not two.
    At 1e-7° a 1.4 m walking step is just outside the one-byte varint window; at
    1e-5° it is comfortably inside. §3.2 has the table;
  - what a track's **timebase and datum** are, which nothing currently states,
    and what happens to a recording when the clock steps.
- **Acceptance:** host tests with golden vectors; a recording that survives a
  simulated crash at an arbitrary point; the simplifier's error bound asserted
  rather than eyeballed.
- **Research status:** done — §3
- **Implementation status:** not started
- **Tests:** host, entirely. Nothing here needs a radio.
- **Hardware required:** no.

### T-066 · One track, three carriers
- **Priority:** P2, after T-065
- **Dependencies:** T-065, T-043, T-050, [ADR-0002](docs/adr/0002-companion-is-optional.md)
- **Goal:** send and receive a track over the mesh, over BLE directly and over
  IP — one format, three carriers, and nothing above the capability registry
  learns which one it came from.
- **The constraint that shapes it, and it is not negotiable by design:** a
  1000-point track costs 26 packets and **16.5 s of originator airtime** at
  4 bytes per point over LoRa at SF7/62.5 kHz; a 3600-point track, 93 packets
  and 58.9 s. The same track over BLE is one small transfer. So the format must
  carry **the same track at a fidelity chosen for the carrier** — and a track
  that arrived simplified must record that it was simplified, or the map lies
  quietly.
- **What the research did not settle and this task must:** what a half-received
  track looks like and whether the receiver knows it is half; resumption when
  the node goes out of range mid-transfer, which is one of the two most concrete
  instances of ADR-0004's availability states this project has produced; and
  what the user is told in either case.
- **Out of scope, deliberately:** the internet leg beyond the encoding. That
  needs a server, an account model, TLS on a constrained device, a retention
  policy, an operator and a privacy policy, none of which exist — §3.4.
- **Acceptance:** host tests over a simulated lossy link; a transfer interrupted
  at every chunk boundary resumes or fails legibly, and never silently truncates.
- **Research status:** done — §3
- **Implementation status:** not started
- **Tests:** host. The LoRa airtime figures are `ESTIMATED, NOT EXECUTED`.
- **Hardware required:** for the airtime numbers, yes.

### T-067 · The reuse ledger owes three records
- **Priority:** P1 — `CLAUDE.md` requires the record *before* the code, and
  three subsystems are queued behind these
- **Dependencies:** none. It is reading.
- **Goal:** close three gaps the ledger's own section list confirms:
  - **`xioTechnologies/Fusion`** — read, MIT, © 2021 x-io Technologies.
    Decision is likely `USE AS DEPENDENCY` for its stationary-bias machinery
    specifically, and explicitly **not** for heading: without a magnetometer,
    yaw is unobservable and no filter makes it observable. Record why the
    narrower use is the useful one.
  - **track geometry and trajectory compression** — no record exists at all.
    `psimpl` and `simplify-js` licences are **unchecked**; Zhao-Saalfeld's
    reference code has no licence stated. Nothing may be depended on first.
  - **BLE beacons and tag ecosystems** — no record exists. The licences are
    already established in §1.5 and three of them are blocking; write it down so
    the next agent does not re-derive it.
- **And one that is already load-bearing:** `OPEN_QUESTIONS` M14 records that
  `rweather/Crypto` has never had its licence read, and it is the **active**
  Ed25519 verify path after M12. A track transfer would ride it. Close M14 here.
- **Acceptance:** four records using the file's own template, copied whole. A
  half-filled record is worse than none.
- **Research status:** partly done — §1.5 and §2.5
- **Implementation status:** n/a
- **Tests:** none.
- **Hardware required:** no.

### T-068 · Can either board sleep on a 32 kHz clock
- **Priority:** P1 — it is a **14×** lever on idle current and it gates every
  always-on feature, not only beacons
- **Dependencies:** none for the reading; a board for the confirmation
- **Goal:** establish whether the SoC can run its RTC from a 32.768 kHz source
  on each board. ESP-IDF reports 3.3 mA in light sleep on the main crystal
  against **230 µA** on a 32 kHz one.
- **What is already known, and it is tantalising rather than sufficient:** the
  T-Watch has a PCF8563 emitting 32.768 kHz on `CLKOUT` by default (open-drain,
  enabled at power-on), `HARDWARE_MATRIX` records the net as present with R126
  not fitted — and `GPIO15`, which is the S3's `XTAL_32K_P`, is `VERIFIED` as the
  MAX98357A I²S word clock. So the two may be mutually exclusive on that board.
  Where `RTC_CLKOUT` terminates is unrecorded. For the Waveshare nothing is
  established at all, and its PCF85063 is a different part.
- **Note:** ESP-IDF offers four RTC sources on the S3, not two —
  `RTC_CLK_SRC_EXT_OSC` accepts an external oscillator with no crystal fitted,
  which is a route the schematic question does not close.
- **Acceptance:** a row per board in
  [VERIFIED_FACTS](docs/research/VERIFIED_FACTS.md), each with the schematic
  sheet or the document it came from. The current figure is a HIL plan, not this
  task.
- **Research status:** not started
- **Implementation status:** n/a
- **Tests:** none.
- **Hardware required:** no for the documents; yes to confirm a current.

### T-069 · Attadipa read against the tracker threat model, and the law about children
- **Priority:** P1 — it applies to what is **already specified**, not only to
  anything new
- **Dependencies:** none
- **Goal:** the repository has never asked whether Attadipa is itself a device
  the unwanted-tracking work exists to detect. `grep -rni "stalk|tracker detect"
  docs/` returns nothing.
- **Why it is not hypothetical:** the product as specified is a wearable that
  reports a person's position to a remote party over a mesh; Child Mode makes
  that person a six-year-old; DULT's own scope enumerates "Watch" as an
  accessory category (value 146 in the Accessory Category table — confirmed in
  [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md) §1.3); and
  T-066's track exchange is a location-sharing channel that has never been read
  against a threat model at all.
- **Where to start, so this is not re-derived:** `draft-ietf-dult-threat-model-05`
  is an **active** IETF working-group document (latest revision 2026-08-06,
  not expired like the accessory protocol) and is the primary source naming
  what DULT itself considers a threat — found but not read in full by T-070's
  research; [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md)
  §"Relationship to T-069" hands it over.
- **Second half, and it is specific rather than general:** a child's position
  leaving the device engages GDPR Article 8 (consent, thresholds 13–16 by member
  state), the UK Age Appropriate Design Code and COPPA. Google scoping Find Hub
  to "age-eligible users" was read as a feasibility signal; it is a hint that
  the law here is particular.
- **Third half, which the research also found missing:** there is no
  authorization model for who may receive a position and no revocation story.
  `NODE_PROFILE` N5 already asks what a shared node means; nobody has asked what
  a node **learns** about a watch that pairs with it, or what happens to that
  knowledge when the pairing ends.
- **Acceptance:** a threat-model document naming who can learn a wearer's
  position, through which path, and what revokes it — plus an explicit statement
  of which questions are legal advice this project cannot give itself.
- **Research status:** not started
- **Implementation status:** n/a
- **Tests:** none.
- **Hardware required:** no.

### T-070 · The watch as a tracker detector, which is the opposite feature
- **Priority:** P2
- **Dependencies:** T-069 for implementation. The research half did not need
  T-069 and was done directly — see below.
- **Goal:** scan for an unknown BLE identifier that has stayed near the wearer
  for an implausibly long time, and say so.
- **Why it is worth more than emulation for this product:** it **protects** the
  wearer rather than exposing them, which is the right direction for a
  child-worn device; it needs no ecosystem's approval, no account and no server;
  it uses a radio the watch certainly has; and `seemoo-lab/AirGuard` is
  Apache-2.0, MIT-compatible and actively maintained, so there is something to
  learn from rather than invent.
- **The honest limit, stated up front, and now sourced rather than deferred.**
  [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md) §3: two
  independent 2025/2026 studies — one peer-reviewed (PoPETs 2025), one an
  unreviewed 2026 preprint — report that an identifier rotated faster than a
  detector's correlation window evades or substantially delays Apple's,
  Google's **and AirGuard's** detection, on every ecosystem except Samsung's
  aging-counter scheme. Both used an ESP32 to demonstrate it. The exact 2022
  methodology against a *current* iOS build remains untested and `UNKNOWN`.
  **Do not ship a detector that implies it catches everything** — AirGuard's
  own shipped strings do not, and neither should Attadipa's.
- **What the research also settled, so implementation does not re-derive it —
  all in [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md):**
  AirGuard's actual thresholds (3 sightings / 14 days, ≥2–4 distinct locations
  150 m apart, altitude gates for the aeroplane case — §2); DULT's current
  broadcast format and rotation intervals, and that no shipping accessory has
  been observed using DULT's own `0xFCB2` service data yet (§1); that
  Espressif publishes no BLE-scanning current figure at all — 93 mA RX peak is
  the nearest documented proxy, and the scanning power story is still gated on
  T-068's open question of whether either board can reach a 32 kHz sleep floor
  between scan bursts (§4); that concurrent scan-while-connected is documented
  as supported and costs 828 B per activity, but its cost to the companion
  link's latency is undocumented (§5); and a correction — ADR-0003 does not
  claim a shared T-Watch BLE/LoRa front end, contrary to how this task was
  first framed (§5.3).
- **Acceptance:** host tests over recorded advertisement sequences — a
  co-travelling identifier is flagged, a shop full of stationary beacons is not.
- **Research status:** done —
  [`TRACKER_DETECTION.md`](docs/research/TRACKER_DETECTION.md); reuse ledger
  record added.
- **Implementation status:** not started
- **Tests:** host, over synthetic scan traces.
- **Hardware required:** for a real scan and for the power figures, yes — see
  T-068, which this task's power story now depends on explicitly.

### T-071 · Dead reckoning: odometry, the disk, and what makes it stop
- **Priority:** P2 — **not blocked.**
  [OD-13](docs/research/OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature)
  §2 answers question 3 without being asked it: everything is built around
  getting back on foot, which is the one purpose that survives the physics.
  Build for *"get me back to the tent"*, not for *"reconstruct my route"* —
  the second is T-088, where GNSS is present and reckoning is not needed.
- **Dependencies:** T-060, T-061 (it is the same step detector, not a second
  one), T-065, [ADR-0009](docs/adr/0009-heading.md)
- **Goal:** when GNSS is lost, say how far the wearer has walked and where they
  might be — and say it in a form that cannot be mistaken for a fix.
- **The shape, and the rule that outranks the feature:** DR consumes odometry,
  an anchor and — where it exists — `Heading`. **It never manufactures a
  heading.** [#21](https://github.com/hleserg/Attadipa/issues/21) has just
  removed accel+gyro fusion from `Capability::Heading` because without a
  magnetometer yaw is unobservable; dead reckoning must not bring it back
  through a side door.
- **What the numbers permit, from §2.1:** an uncalibrated gyroscope offset of
  ±10 dps is a full 360° of heading error in 36 seconds, and thermal drift alone
  over a 10 °C swing is 30°/minute. On the T-Watch there is no gyroscope at all,
  so a turn is **not observable** and the reckoned track is a length, not a
  shape. The honest output is therefore **a disk** of radius
  `r₀ + d̂·(1 + ε)` — "somewhere within 300 m of the anchor" — which is drawable
  and true, rather than a line which is neither.
- **What must be decided rather than assumed:**
  - **what expires the disk.** ADR-0011 already says a good-sixty-seconds-ago
    position is a circle; nothing says when the circle stops being drawn. The
    radius bound is silently false the moment the wearer boards a bus — steps
    stop, distance does not;
  - **it is two devices.** Per OD-1 the GNSS is on the node and the IMU is on
    the wrist, and the Waveshare has no GNSS at all. The anchor and the step
    count cross a link that can drop, and so does the stride calibration;
  - **stride is calibrated against GNSS while GNSS is good** — the difference is
    <2 % of distance calibrated against −20 % uncalibrated. Until enough good
    distance has been seen the stride is `Uncalibrated` and says so;
  - **the gyroscope is a mode, not a service.** It costs 651–908 µA against
    30–55 µA accel-only, roughly 46× the BMA423's always-on step counting.
- **The highest-leverage piece:** extend the replay rig to carry IMU samples.
  The whole reckoning path then becomes deterministic and host-testable, exactly
  as `classify()` already is, and no part of it waits on a board.
- **Acceptance:** replayed acceleration traces produce a defensible step count
  and a disk whose radius is asserted, not eyeballed; the T-Watch profile
  produces a length and refuses to produce a shape.
- **Research status:** done — §2
- **Implementation status:** not started
- **Tests:** host, through the replay rig. Every accuracy figure that matters in
  the field is `NOT EXECUTED — HARDWARE REQUIRED`.
- **Hardware required:** for accuracy, yes. For the logic, no.

### T-060b · The Bosch application note itself, for what revision 1.1 lacks
- **Priority:** P3, `nice-to-have`. **Nothing blocks on it** — T-060a closed the
  questions T-061 needed.
- **Dependencies:** T-060a (**done**)
- **Goal:** obtain `BST-MAS-AN032` (*Wearable Feature Set*) and answer what
  datasheet revision 1.1 does not: BMA423 step-counter behaviour at the 32-bit
  boundary, and whatever tuning guidance sits behind the datasheet's *"with the
  support of the corresponding field application engineer"*.
- **What has already been tried and failed:** `bosch-sensortec.com` (HTTP 403,
  three attempts), Mouser (403), LCSC (HTML only), Octopart, DigiKey,
  micro-semiconductor (product flyer), watchy.sqfmi.com (revision 1.1 datasheet,
  not the note). Untried: Bosch's community forum attachments, the
  `BMA456`/`BMA400` sibling notes, an account-gated distributor download.
- **Acceptance:** the boundary question marked in
  [PEDOMETER_PARTS §1.8](docs/research/PEDOMETER_PARTS.md) with the document
  revision, or a note saying the note does not answer it either.
- **This is a research task.** No code comes out of it.
- **Hardware required:** no.

### T-061 · Steps, as a capability with a power story
- **Priority:** P1, after T-060
- **Dependencies:** T-060 (**done**), T-060a (**done** — the power story is
  `13–14 µA at 50 Hz, low-power mode`),
  [ADR-0007](docs/adr/0007-two-capability-layers.md),
  T-046 (crash-safe persistence), T-045 (`PowerState`)
- **Goal:** implement `Capability::MotionSensing` for step counting, on both
  boards, without either board's answer leaking upwards.
- **The shape:** an application asks for a step count and a daily total. It
  never learns whether a sensor counted them or firmware did, which interrupt
  fired, or that one board can count through a sleep and the other may not.
- **What has to be decided rather than assumed:**
  - a board that cannot count while asleep reports `MotionSensing` as
    **`Degraded`** with a reason, not as a number that is quietly missing hours.
    A mandatory pedometer that stops when the screen goes off is not one;
  - the daily total survives a reboot, a crash and a flat battery, and is zeroed
    by midnight and by nothing else. Four events, one of which resets it;
  - **no interpolation.** A period the device was not measuring did not contain
    a known number of steps. The day's total says steps were missed rather than
    inventing them — the same rule the GNSS work applies to a position nobody
    observed.
- **Acceptance:** a host test with a synthetic acceleration trace replayed
  through the same path the device uses — the replay rig's shape, a second
  reader; both board profiles produce a defensible availability; the daily total
  survives a simulated crash at an arbitrary point.
- **Research status:** T-060 and T-060a are **done**; the research that remains
  is two hardware questions, not one task.
  - **Which IMU the Waveshare carries.** `QMI8658C` has a pedometer;
    `QMI8658A` Rev D had it deleted. The board is recorded as
    "QMI8658 / QMI8658C" and the schematic prints no revision, so a mandatory
    pedometer (OD-6) may have no hardware on one of the two boards. Same shape
    as [ADR-0003](docs/adr/0003-radio-not-lora.md)'s radio question, in a
    second subsystem — [PEDOMETER_PARTS.md](docs/research/PEDOMETER_PARTS.md)
    §2.1. Settled by reading `WHO_AM_I` and the revision register on a board.
  - **Whether the PMU keeps the IMU rail up across an SoC sleep.** This is
    **[H8](docs/research/OPEN_QUESTIONS.md)**, already filed and already
    holding the schematic-level detail: the vendor document says ALDO1 is
    unused, the schematic shows it driving `+3V3`, and `+3V3` is what feeds the
    BMA423. It was raised again in this task's research without the
    cross-reference, which would have sent two people at the same question from
    two directions. Whoever resolves H8 unblocks this.
- **Implementation status:** not started
- **Tests:** host, plus a HIL plan for the wake rate and the current, which is
  the only way the power claim becomes a measurement.
- **Hardware required:** for the power numbers, yes. For the logic, no.

### T-051 · What the MIA-M10Q actually does, from u-blox's own documents
- **Priority:** P1 — it gates the GNSS driver, and nothing before it
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md)
- **Goal:** fill in the receiver capability descriptor for the u-blox MIA-M10Q
  from primary sources only, in the order the owner gives: datasheet →
  integration manual → interface/protocol specification → vendor examples →
  official library source.
- **What to answer, at minimum:** `UBX-SEC-SIG` and `UBX-SEC-SIGLOG` — what
  they report and on which firmware; the jamming and spoofing indications and
  what each state means; `CFG-ITFM-*`; `UBX-NAV-PL` and whether a protection
  level is produced at all on this part; fix and time validity flags; per-signal
  C/N0; constellation control; Super-S; AssistNow Autonomous and what the
  official assistance mechanism is; backup and hot start, and what the MS412FE
  on the daughterboard actually backs up; configuration lockdown; message
  integrity; secure boot. **And: does it accept RTCM corrections?** — OD-5 says
  no, and the owner's technical claims are not automatically facts.
- **Acceptance:** every descriptor entry is `SUPPORTED`, `UNSUPPORTED` or
  `UNKNOWN`, each with the document and section it came from. `UNKNOWN` is a
  valid answer and an unsourced `SUPPORTED` is not.
- **Research status:** not started
- **Implementation status:** not started — no code comes out of this task
- **Tests:** none. It produces a research record in `docs/research/`.
- **Hardware required:** no for the documents. Confirming any of it on a fitted
  module is a separate line, and until then nothing here is `HARDWARE-VERIFIED`.

### T-052 · What the Quectel LS550G actually does, and what it only claims
- **Priority:** P1
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md)
- **Goal:** the same descriptor for the second variant. The vendor advertises
  jamming detection, active anti-jamming, a multi-tone interference canceller,
  an internal LNA, multi-constellation operation, EPOC and power saving.
- **Acceptance:** each advertised feature is either traced to a primary source
  or recorded as a **claim**. **Anti-spoofing stays `UNKNOWN` until a primary
  source or a real device says otherwise** (OD-5 §2) — the marketing page is
  not a source. The two rails this variant needs (DC4 at 850 mV *and* BLDO1) are
  re-confirmed against the datasheet, because getting that wrong means GNSS
  silently never starts.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** none — a research record.
- **Hardware required:** no for the documents; yes to close anything the
  documents do not answer, which on this part is expected to be most of it.

### T-053 · The simulator can fail at GNSS twelve different ways
- **Priority:** P2 — after the descriptor research, before the trust engine
- **Dependencies:** [ADR-0011](docs/adr/0011-gnss-integrity.md), T-051, T-052
- **Half of this is built.** The offline half of the acceptance criterion — *"a
  captured trace can be replayed into the trust engine offline (ADR-0011 §7)"* —
  exists: `tests/replay/` with twelve traces covering the twelve failures below,
  a runner, and a test proving the runner can fail. What remains is the
  *simulator* half: making each of the twelve selectable from the command line
  so a screen can be developed against them, and confirming that none of them
  renders as another one. The traces are the specification for that work — the
  same twelve, in a format the simulator can read.
- **Goal:** injectable GNSS scenarios in the simulator, so the trust state and
  every screen that reads it can be developed and reviewed without a sky.
- **The twelve, from OD-5:** normal · weak signal · fix loss · poor accuracy ·
  stale position · a large jump while the accelerometer says stationary ·
  receiver-reported jamming · receiver-reported spoofing · an invalid protection
  level · two providers disagreeing · `Ready` with `NO_FIX` · `Ready` with a
  valid fix and `Untrusted`.
- **Acceptance:** each is selectable from the command line and reproducible;
  each produces a different visible outcome, and none of them renders as another
  one; a captured trace can be replayed into the trust engine offline
  (ADR-0011 §7).
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host — each scenario asserts the trust state and the reason codes
  it must produce.
- **Hardware required:** no. This is the task that exists *because* the
  interesting failures cannot be staged on hardware.

### T-043 · The node link is not a BLE link
- **Priority:** P0 — it is the shape of the transport, and the shape is cheapest
  before there is code in it
- **Dependencies:** [ADR-0005](docs/adr/0005-node-protocol.md)
- **Goal:** a transport abstraction for the watch↔node link that admits several
  interfaces at once — BLE, USB, UART, and later Wi-Fi/ESP-NOW — the way
  MeshCore's `MultiSerialInterface` (#3049, merged) does, without the three
  semantics that make upstream's version wrong for us.
- **Acceptance:** a reply goes back to the interface its request arrived on, not
  to all of them; each interface has its own bounded queue, so a stalled one
  cannot mark the stack busy; interfaces are serviced fairly rather than in
  registration order; a write failure names the interface that failed. No
  transport-specific method (`enableBluetooth()`) on the generic manager.
- **Research status:** done —
  [meshcore-1.17-review §1](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — two fake interfaces, one of which never drains; the other
  must keep working, and the drop must be counted.
- **Hardware required:** no for the abstraction; yes to prove it on a real link.

### T-044 · Framing that can be resynchronised
- **Priority:** P0 — it is a two-line requirement now and a protocol break later
- **Dependencies:** T-043
- **Goal:** write the framing requirements into
  [ADR-0005](docs/adr/0005-node-protocol.md). MeshCore's USB framing is a start
  byte plus a 16-bit length with **no checksum, no escaping and no resync
  marker**, and an over-long frame is silently truncated to `MAX_FRAME_SIZE` and
  delivered as if complete.
- **Acceptance:** ADR-0005 states that a torn frame must be *detected*; that an
  over-long frame is an error rather than a truncation; that resynchronisation
  cannot depend on a byte value that occurs freely in payloads; and that
  connection state is either observable or explicitly `Unknown`, never a
  hardcoded `true`.
- **Research status:** done —
  [meshcore-1.17-review §2](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a fuzz over truncated, extended and bit-flipped frames; no
  input may produce a frame the parser reports as valid.
- **Hardware required:** no.

### T-045 · `PowerState`: hibernate is not a sleep with the radio armed
- **Priority:** P0
- **Goal:** the six-state power model — `ACTIVE`, `IDLE`, `LIGHT_SLEEP`,
  `MESH_LISTEN_SLEEP`, `HIBERNATE`, `POWER_OFF` — as a type, with the wake
  sources of each written down.
- **Why:** upstream's `HeltecV4R8Board::powerOff()` is `enterDeepSleep(0)`, and
  that path leaves the FEM in RX and arms EXT1 on `P_LORA_DIO_1`. "Off" ends at
  the next packet (#3165; fix #3168 still open). Two behaviours that differ only
  in their wake sources shared one function name.
- **Acceptance:** a board cannot satisfy `HIBERNATE` while a radio wake source is
  armed — the API must not let it, rather than a review catching it. Each state
  names its wake sources and its expected current as `ESTIMATED` until measured.
- **Research status:** done —
  [meshcore-1.17-review §5](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a fake board that arms a radio wake in `HIBERNATE` must fail.
- **Hardware required:** yes to fill in the current figures. Until then every
  number is `ESTIMATED` and says so.

### T-046 · Crash-safe persistence, for everything and not only contacts
- **Priority:** P1
- **Goal:** one rule for every persistent structure —
  `write temp → flush → close → rename old to backup → atomic rename into place`
  — with load falling back to the backup and reporting which copy it used.
- **Why:** upstream still writes `/contacts3` in place and `break`s mid-stream on
  failure (open PR #1447 fixes contacts only), and `savePrefs()` on nRF52/STM32
  *deletes* `prefs.json` before writing the new one. Its JSON migration (#2982)
  is the part to copy: forward-convert and **leave the old file alone**.
- **Acceptance:** no critical structure is ever overwritten in place; a migration
  never destroys its source; the ~2× storage headroom the pattern needs is
  checked rather than assumed; dirty state is flushed on every shutdown and
  reboot path (#2627).
- **Research status:** done —
  [meshcore-1.17-review §6](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a filesystem fake that fails at every write offset in turn;
  a load must always produce either the old contents or the new, never a hybrid.
- **Hardware required:** no for the logic; yes for the flash behaviour.

### T-047 · Two clocks, and the rule about which one measures time
- **Priority:** P1
- **Goal:** adopt MeshCore's separation — `RTCClock` (wall, absolute) versus
  `MillisecondClock` (monotonic) — as Attadipa's own, and write the rule down:
  **timers, timeouts, retries, connection expiry and the scheduler use the
  monotonic clock.** RTC and GNSS time only where absolute time is required.
- **Why:** a GNSS fix that steps the wall clock must not be able to make a
  timeout fire late — or never. Upstream already hit the long-uptime version of
  this (#2937). It also connects to T-042: a clock that jumps is itself evidence
  in the GNSS trust state.
- **Acceptance:** the two clocks are distinct types, not one type with two
  meanings; a duration cannot be computed from wall-clock readings without
  saying so explicitly.
- **Research status:** done —
  [meshcore-1.17-review §7](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — step the wall clock forwards and backwards under a running
  timeout and assert it fires at the same monotonic instant.
- **Hardware required:** no.

### T-048 · The crypto and RNG seam
- **Priority:** P1
- **Goal:** one crypto interface with three named backends — `software`,
  `ESP32-S3 hardware`, `nRF52 CC310` — and an entropy source that is the
  platform's hardware RNG.
- **Why:** upstream's ESP32 LoRa path uses `radio->randomByte() ^ ::random()`,
  and the Arduino PRNG's own header calls itself *"VERY SLOW"*. `esp_fill_random`
  appears **only** in the two ESP-NOW variants, and there is **no** mbedtls,
  `esp_aes` or `esp_sha` anywhere in the tree. So this is a gap to fill, not a
  port. nRF52 got CC310 in 1.17.0 (#2824); the ESP32 equivalent (#2280) is open.
- **Acceptance:** no `#ifdef` for a backend above the seam; entropy comes from
  `esp_fill_random()` on ESP32; and **no claim that hardware acceleration is
  faster** appears anywhere until it is `MEASURED` against the software path at
  Attadipa's actual payload sizes.
- **Research status:** done —
  [meshcore-1.17-review §8](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — known-answer vectors that every backend must satisfy.
- **Hardware required:** yes for the ESP32-S3 measurement.

### T-049 · Front-end control is a board capability
- **Priority:** P1
- **Dependencies:** [ADR-0003](docs/adr/0003-radio-not-lora.md)
- **Goal:** express FEM/LNA/PA control as a property of the **board**, never as
  something inferred from "it has an SX1262".
- **Why:** the Heltec V4 auto-detects a GC1109 (V4.2) or a KCT8103L (V4.3) from
  the pull level of a shared GPIO at boot, and only one of the two can switch its
  LNA. Same product name, different silicon — the T-Watch radio problem from a
  second vendor. Upstream then shipped the LNA on by default and removed the
  companion's ability to turn it off (`e2aa7b98`, #3203); issues #3010 and #3232
  report the noise floor rising 13–22 dB and are open.
- **Acceptance:** the capability model can express *"has a front end, and it is
  switchable"* separately from *"has a front end"*; no default that changes RF
  behaviour is set without a measurement recorded beside it; the upstream
  implementation is **not** ported.
- **Research status:** done —
  [meshcore-1.17-review §4](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — a board declaring a fixed front end must not compile against
  the switch-it API.
- **Hardware required:** yes to measure any of it. No Heltec board is in this
  project's hands.

### T-050 · The MeshCore adapter boundary, tested before there is an adapter
- **Priority:** P1
- **Dependencies:** [ADR-0007](docs/adr/0007-two-capability-layers.md) §5
- **Goal:** the boundary the owner asked for —
  `UI/Apps → Services → Mesh Service API → MeshCore Adapter → transports → HAL`
  — enforced the way the other two boundaries already are: a `PRIVATE` link plus
  a fixture that **must fail** to compile.
- **Acceptance:** no file outside the adapter includes a MeshCore header, and a
  test proves it by trying; bumping the MeshCore pin cannot require an edit
  above the adapter.
- **Research status:** done —
  [meshcore-1.17-review §12](docs/upstream/meshcore-1.17-review.md)
- **Implementation status:** not started
- **Tests:** host — the third boundary test, alongside
  `capability_boundary_negative` and `l10n_boundary_negative`.
- **Hardware required:** no.

### T-013 · The local mesh integration spike
- **Priority:** P0
- **Dependencies:** T-006 (**done**), [ADR-0003](docs/adr/0003-radio-not-lora.md),
  [ADR-0008](docs/adr/0008-mesh-service-providers.md)
- **Goal:** [ADR-0008](docs/adr/0008-mesh-service-providers.md) §5 deliberately
  does **not** choose how a watch runs a local mesh stack, because final §14
  forbids choosing without a measured spike — and this project already made that
  mistake once. Produce the numbers.
- **Options to measure:** direct component integration · an isolated
  compatibility layer · upstreamable ESP-IDF work · a narrow Arduino
  compatibility island · supporting only the combinations that are viable.
- **Acceptance:** for each option — flash cost, internal RAM cost, what an
  Arduino shim actually pulls in, whether MeshCore's file-static radio state
  (M9) can be tolerated under `HardwareCoordinator`, and how much routing
  behaviour would have to be re-derived. Plus the standing obligation: re-run
  `grep RADIO_CLASS variants/` against the pinned revision, because upstream
  adds radios and the matrix is wrong the moment it stops being checked.
- **Research status:** the compatibility matrix is done
  ([ADR-0003](docs/adr/0003-radio-not-lora.md)); the cost spike is not
- **Implementation status:** not started
- **Tests:** the spike's own builds
- **Hardware required:** for a working link, yes — and **two** radio devices
  (A3). For the cost numbers, no.
- **Constraint that is already fixed:** `Arduino.h` does not enter `core/`.

### T-016 · Benchmark the node protocol encoding, then accept or replace it
- **Priority:** P1
- **Dependencies:** [ADR-0005](docs/adr/0005-node-protocol.md) (**provisional**)
- **Goal:** final §18 endorses ADR-0005's *goals* and rejects its *evidence*: it
  compared a hypothetical small Attadipa TLV against Meshtastic's entire
  `meshtastic_FromRadio` union and called the question settled. That is not a
  comparison.
- **Acceptance:** an Attadipa TLV prototype, nanopb with an Attadipa-specific
  streaming/callback schema, and at least one other compact option, measured on
  `xtensa-esp32s3-elf-gcc` for peak internal RAM, static RAM, flash, encoded
  bytes, malformed-input behaviour, schema-evolution cost, tooling, fragmentation
  interaction and test burden. If TLV still wins, accept ADR-0005 with the
  evidence.
- **Also required before ADR-0005 can be accepted:** the demultiplexing rule
  (final §19) — how a parser distinguishes log text, MeshCore companion frames
  and Attadipa frames on one physical link. Separate GATT characteristics,
  separate UART channels, or an explicit outer mux frame. A diagram is not a
  design.
- **Research status:** nanopb measured in isolation; the Attadipa-schema
  comparison is the missing half
- **Implementation status:** ADR written, provisional
- **Tests:** round-trip vectors; a hostile-frame corpus; a version-mismatch test
- **Hardware required:** no

### T-035 · ADR: rail ownership and reference counting
- **Priority:** P1
- **Dependencies:** none
- **Goal:** ALDO3 feeds the display **and** the touch controller on the T-Watch;
  BLDO2 gates the haptic driver's enable. A rail with two consumers needs an
  owner and a discipline, or the second consumer turns the first one off.
- **Acceptance:** who may request a rail; reference counting or another argued
  mechanism; what happens when a rail is requested during a sensitive operation;
  how ownership interacts with final §32's rule that a valid owned state can be
  "untouched".
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests over a simulated PMU
- **Hardware required:** for real sequencing timing, yes

### T-018 · Application framework: surviving the loss of a capability
- **Priority:** P0
- **Dependencies:** T-015 (**done**)
- **Goal:** the lifecycle verbs (final §59) do not include *"the GNSS you were
  navigating with has just left the building"*. With a detachable node that is an
  ordinary Tuesday.
- **Acceptance:** an application declares required and optional capabilities;
  the framework guarantees delivery of a capability change to open applications;
  the launcher rule settled for "installed when the capability existed, opened
  when it does not"; no application queries provider state directly.
- **Research status:** partial — InfiniTime's app model is the closest mature
  comparable
- **Implementation status:** the contract is sketched in
  [ADR-0004](docs/adr/0004-capability-sources.md) §5; the framework is not built
- **Tests:** attach → open → detach → reattach, in host tests, with no hardware
- **Hardware required:** no

### T-024 · ADR: the event bus and the concurrency model
- **Priority:** P1
- **Dependencies:** T-018
- **Goal:** final §60 and §61 — who owns which task, what may block, how events
  are delivered, back-pressure, queue bounds, UI-thread rules, interrupt handoff.
  Choosing late means choosing several.
- **Acceptance:** one mechanism; bounded queues; defined behaviour for a slow
  consumer; stack usage countable, because it is a memory-budget line.
- **Research status:** partial
- **Implementation status:** not started
- **Tests:** host tests
- **Hardware required:** no

### T-025 · ADR: partitions, NVS and OTA — for two devices
- **Priority:** P1
- **Dependencies:** T-004, T-017 (**done**)
- **Goal:** flash layout, settings storage and firmware update. The node makes
  OTA a compatibility question rather than a delivery one: two devices updated
  independently that must keep talking.
- **Acceptance:** a partition table per board (16 MB T-Watch, 32 MB Waveshare,
  both VERIFIED); rollback; settings survival across update; behaviour when the
  two firmware versions differ by more than the protocol allows.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** host tests for the version-compatibility matrix
- **Hardware required:** for the flashing path yes; for the compatibility logic no

### T-026 · Implement the honest heading model
- **Priority:** P1
- **Dependencies:** [ADR-0009](docs/adr/0009-heading.md) (**done**), A5/A6
- **Goal:** the *decision* is made — three quantities, explicit reference frames,
  no `UserBody`, a node's compass is the node's. What remains is building it and
  designing the states this hardware is actually in.
- **Acceptance:** the `Heading` structure and its validity states; the Navigator
  state table from ADR-0009 §5 rendered, including *standing still* as a
  designed screen rather than a blank dial; **no configuration of inputs draws a
  wrist-relative arrow from a `NodeBody` or `CourseOverGround` source.**
- **Research status:** done
- **Implementation status:** not started
- **Tests:** host tests over recorded NMEA including stationary traces; a
  simulator scenario per state
- **Hardware required:** no for the logic; yes for a real fix, and **H10** (the
  speed gate) is a measurement on the fitted module, not a number to choose

### T-027 · Airtime and duty-cycle accounting
- **Priority:** P1
- **Dependencies:** T-006 (**done**), T-017 (**done**)
- **Goal:** the regulated settings are bounded by rules that constrain
  **airtime**, and the reference data model measures none of it
  ([OD-2](docs/research/OWNER_DECISIONS.md)). A device that cannot measure its
  own duty cycle cannot demonstrate compliance (final §38).
- **Acceptance:** airtime computed per transmission and accumulated per band;
  the limit part of the regulatory profile, not a constant; visible in
  diagnostics; the arithmetic tested against reference time-on-air formulas.
- **Research status:** MeshCore's own governor found — `Dispatcher::updateTxBudget()`
  — which Attadipa must reconcile with rather than override on a local mesh path
- **Implementation status:** not started
- **Tests:** host tests against known LoRa time-on-air arithmetic
- **Hardware required:** no

### T-028 · Three-valued telemetry, and staleness on everything
- **Priority:** P1
- **Dependencies:** T-015 (**done**)
- **Goal:** the reference model shows `Node count: Unknown` — neither a number
  nor zero — and carries **no timestamp on anything**, so a four-hour-old
  coordinate and a two-second-old one are the same two numbers.
- **Acceptance:** a shared vocabulary for *known* · *known to be none* · *not
  known*; every datum crossing the link carrying its two ages and, where it is a
  measurement, its validity; a UI rule that never renders "not known" as "none".
- **Research status:** not started
- **Implementation status:** the model is in
  [ADR-0004](docs/adr/0004-capability-sources.md) §3; nothing is built
- **Tests:** host tests that a stale value cannot render as fresh
- **Hardware required:** no

### T-029 · Data feeds are not capabilities
- **Priority:** P1
- **Dependencies:** T-015 (**done**)
- **Goal:** final §16 lists what a node may provide and mixes two kinds of thing
  — capabilities (mesh connectivity, position) and feeds (weather, Home
  Assistant events, quest events, telemetry). A `Capability::Weather` would be a
  category error.
- **Acceptance:** the two modelled separately, with the boundary stated and the
  test that decides which side a new thing falls on.
- **Research status:** decided in
  [ADR-0004](docs/adr/0004-capability-sources.md) §4 and
  [ADR-0007](docs/adr/0007-two-capability-layers.md) §2
- **Implementation status:** not started
- **Tests:** host tests
- **Hardware required:** no

### T-030 · Adversarially break the capability model before building on it
- **Priority:** P0
- **Dependencies:** T-015 (**done**), T-007
- **Goal:** the model is about to become load-bearing for every application.
  Find where it gives a *wrong answer*, not where it is merely incomplete.
- **Scenarios that must each produce a defensible answer:** the link drops
  mid-navigation · the node's battery dies during an SOS · two watches share one
  node · a fix arrives ninety seconds stale · an application is installed when a
  capability exists and opened when it does not · the node is connected but its
  own GNSS has no fix · the node's firmware is too old to speak our version ·
  the user disables the node's radio from the watch and thereby cuts the link ·
  **a T-Watch whose radio is a CC1101** · **a node attached to a watch that
  already has a working local mesh**.
- **Acceptance:** every scenario resolved in the model or recorded as a defect.
  The sharpest remains: "node connected" and "node has data" are different
  states, and a model that collapses them reports a position the device does not
  have.
- **Research status:** n/a
- **Implementation status:** not started — the six adversarial agents allocated
  to this terminated on an account spend limit and returned nothing
- **Tests:** each scenario becomes a host test
- **Hardware required:** no

### T-007 · Reuse survey of existing firmware for these boards
- **Priority:** P1
- **Dependencies:** none
- **Goal:** several open-source firmwares already target these exact boards.
  Examine them before writing equivalents (final §64).
- **Candidates:** `MarcoRR/S3NTRY`, `joaquimorg/OLEDS3Watch` (ESP-Brookesia),
  `infinition/waveshare-watch-rs` (Rust), the LilyGoLib examples, Meshtastic's
  T-Watch support.
- **Acceptance:** a reuse-ledger record each, with a decision from the ledger
  vocabulary and a licence check.
- **Research status:** candidates identified; clones in `/root/upstream`
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-020 · Node pairing, identity and trust
- **Priority:** P1
- **Dependencies:** T-016
- **Goal:** whether the watch has its own mesh identity that the node merely
  carries, or is a client of the node's identity, is question N4 — and the two
  produce different security models, message histories and privacy exposure.
- **Acceptance:** pairing flow specified; the trust boundary stated; node input
  treated as untrusted exactly as companion input is (ADR-0002 rule 4); what a
  stolen or hostile node can and cannot do, written plainly.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** the hostile-node cases must be host-testable
- **Hardware required:** no

### T-021 · The backlog the node creates
- **Priority:** P2
- **Dependencies:** T-015 (**done**), T-016
- **Goal:** the node adds work across power, coexistence, UI states, diagnostics,
  settings and the simulator. Record it as a gated backlog rather than letting it
  arrive as surprises.
- **Acceptance:** one backlog file, a gate per item, everything hardware-bound
  marked so.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** n/a
- **Hardware required:** no

### T-022 · Simulator: node attach and detach as a first-class state
- **Priority:** P1
- **Dependencies:** T-008, T-015 (**done**)
- **Goal:** the node is a product state that cannot be tested on hardware that
  does not exist. Final §57 requires simulated provider attach and detach,
  simulated stale data and a provider that is `Ready` with no fix.
- **Acceptance:** every state in the ADR-0004 model reachable from the simulator
  without a rebuild, including the ones a real node would make hard to produce
  on demand.
- **Research status:** not started
- **Implementation status:** not started
- **Tests:** this *is* test infrastructure
- **Hardware required:** no

### T-004 · ESP-IDF version decision
- **Priority:** P1 — lowered from P0. It blocks embedded work; it does not block
  M1, which is the simulator.
- **Dependencies:** none
- **Goal:** pin ESP-IDF with recorded reasoning.
- **Acceptance:** a row in [DEPENDENCIES](docs/research/DEPENDENCIES.md) with
  source, version, licence, rationale and upgrade strategy.
- **Research status:** narrowed — Waveshare supports v5.5.5 and v6.0.2, its BSP
  needs ≥ 5.3; LilyGO's PlatformIO pin to IDF 4.4.7 probably does not bind
  Attadipa (T7)
- **Implementation status:** `v5.5.5-496-gc197d718bcc` installed and **verified**
  by a real `idf.py set-target esp32s3 && idf.py build`. Verified is not decided.
- **Tests:** a trivial esp32s3 build — **passed**
- **Hardware required:** no

---

### T-101 · A formatting rule, and CI that enforces it
- **Renumbered from T-039 on 2026-08-22, and do not renumber it back.** That
  ID already belonged to the M0.5 reconciliation record in `## DONE`, dated
  2026-08-21. Nothing outside this file referenced either, so the live task is
  the one that moves. `python3 tools/docs/check_docs.py` fails if it recurs.
- **Priority:** P2
- **Dependencies:** none
- **Goal:** one `.clang-format`, applied to everything under `platform/`,
  `core/`, `apps/`, `sim/` and `tests/`, checked in CI.
- **Why now rather than later:** there is code to format as of 2026-08-21, and
  the cost of adopting a style grows with every file. `.github/workflows/ci.yml`
  already names this task in its list of what is deliberately absent, which is
  the honest way to carry a gap but not a substitute for closing it.
- **Acceptance:** `clang-format --dry-run --Werror` is green on a fresh
  checkout; the rules are chosen once and not argued again.
- **Research status:** not started. The existing code was written to a
  consistent house style by hand — 4 spaces, 100 columns, Allman braces on
  functions and attached elsewhere — so the job is mostly transcribing what is
  already there rather than choosing.
- **Implementation status:** not started
- **Tests:** the CI job is the test
- **Hardware required:** no

### T-072a · The same protocol, against a node that exists
- **Priority:** P2 — it converts a document full of `read from source` into the
  first `OBSERVED` in this area, and it is now possible where it was not before.
- **Dependencies:** T-072
- **Goal:** speak the companion protocol to a **real** vanilla node and record
  where the reading was wrong. A MeshCore node hangs off Home Assistant on the
  LAN host `doctor`, and a USB node is coming to the development machine. A host
  program — not firmware — is enough: open the TCP socket or the serial port,
  send `CMD_DEVICE_QUERY`, read `RESP_CODE_DEVICE_INFO`, and compare byte for
  byte against §3 of the protocol document.
- **Acceptance:** every claim in the protocol document that the exchange touches
  is marked `OBSERVED` or corrected, with the captured bytes committed as a
  fixture. Claims the exchange does not touch stay as they are — a partial
  confirmation must not be written up as a whole one.
- **Answer first, because it is free:** which transport that node actually has.
  §1's trap is that the build name does not tell you.
- **Hardware required:** yes, but not *our* hardware — this needs a MeshCore
  node, not a T-Watch. That is why it can happen now.

### T-074 · More than one mesh provider at once
- **Priority:** P2 — [OD-7](docs/research/OWNER_DECISIONS.md#od-7--the-companion-is-any-node-not-only-ours)
- **Dependencies:** T-072
- **Note, 2026-08-22:** T-073 was rejected ([OD-12](docs/research/OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence)),
  so this loses its second *concrete* provider. The task stands: write it against
  MeshCore plus a hypothetical second. A list of one is not a design flaw, and
  inventing a provider to populate a list would be worse than reasoning about the
  shape honestly.
- **Goal:** extend [ADR-0008](docs/adr/0008-mesh-service-providers.md) §3 from two
  providers to a list. What `availability(MeshMessaging)` means when two are up
  and one is degraded; deduplicating a message that arrived twice over different
  providers; and the explicit decision that bridging two networks is a **product
  decision with an airtime cost**, never a side effect of both being configured.
- **Acceptance:** an ADR amendment, and applications still have one code path.
- **Hardware required:** no

### T-075 · The position-source inventory, and what each may claim
- **Priority:** P1 — [OD-8](docs/research/OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument)
- **Dependencies:** none
- **Goal:** §4 of the research file — seven sources, each with its accuracy where
  known and its **provenance** always. A fix from the wearer's receiver, one
  relayed from a node on a roof, and a coordinate lifted from somebody else's
  message are three different claims and exactly one is about the wearer.
- **Acceptance:** the provenance column is complete and the user-facing
  consequence is stated: the screen says which, in words.
  [ADR-0011](docs/adr/0011-gnss-integrity.md)'s axes are reused, not replaced.
- **Explicitly out of scope:** any estimator that combines two sources into a
  third number. Selection and fusion are different features and fusion has no ADR.
- **Hardware required:** no

### T-076 · Position and data from the phone
- **Priority:** P2 — [OD-8](docs/research/OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument)
- **Dependencies:** T-075
- **Goal:** what a phone will actually hand over, over which protocol, and what
  survives its permission model. The owner's sentence to design against is *"they
  become the primary navigation instrument"* — which fails if the phone offers
  only an already-smoothed position rather than measurements.
- **Acceptance:** documented per platform, with what is refused as prominent as
  what is offered.
- **Hardware required:** eventually yes, for anything claimed as `OBSERVED`

### T-077 · AGPS is a payload, not a transport
- **Priority:** P2 — [OD-8](docs/research/OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument)
- **Dependencies:** T-051, T-052 — the receivers decide what assistance means
- **Goal:** define the assistance data once — format, validity window, size, what
  it actually buys — and answer delivery separately per channel: internet, BLE,
  LoRa, a companion node. Whether anything useful fits a LoRa budget under the
  duty cycle is the interesting row, and T-027's airtime accounting answers it.
- **Acceptance:** §5 of the research file, including the provider's terms. A
  service that forbids redistribution is not a channel-agnostic payload.
- **Hardware required:** for the timings, yes

### T-078 · The node's cellular option
- **Priority:** P3 — [OD-9](docs/research/OWNER_DECISIONS.md#od-9--the-node-may-carry-a-cellular-modem)
- **Dependencies:** a node part number, which does not exist
- **Goal:** module class, bands, current while registered, and whether it can be
  powered down without losing registration. Plus the two things that are not
  engineering: type approval, and whose name the SIM is in.
- **Status:** `BLOCKED` — needs-owner. Recorded so the question is not reopened
  from scratch.
- **Hardware required:** yes

### T-079 · Positioning from cell towers
- **Priority:** P3 — [OD-9](docs/research/OWNER_DECISIONS.md#od-9--the-node-may-carry-a-cellular-modem)
- **Dependencies:** T-078
- **Goal:** whether a tower database may lawfully be shipped in this product —
  licence, size, regional coverage, update cadence, four separate answers — and
  what accuracy may honestly be claimed. Hundreds of metres to kilometres makes
  it a fallback and an indoor sanity check, not a navigation fix, and the UI must
  say so.
- **Also:** a registered device is locatable by the network whether or not the
  wearer asked. T-069's threat model gains a section, and in Child Mode that has
  a legal answer in some jurisdictions.
- **Hardware required:** for accuracy claims, yes

### T-080 · A standing person does not need a new fix
- **Priority:** P1 — [OD-10](docs/research/OWNER_DECISIONS.md#od-10--a-standing-person-does-not-need-a-new-fix).
  The largest continuous draw on a watch that has GNSS.
- **Dependencies:** T-051 and T-052 (what the receivers can do), T-060 (whether
  the IMU can raise a motion interrupt while the SoC sleeps)
- **Goal:** duty-cycle the receiver against motion. Ask less often when the
  wearer is still; hold an accurate, trusted fix rather than re-measuring it; and
  **do not let that turn the next fix into a cold start**, which is the trap the
  owner named in the same sentence as the idea.
- **Acceptance:**
  - standing still is a hypothesis, not a fact — a rate reduction with a
    **ceiling**, never an indefinite suspension, and the ceiling is a setting;
  - a held position is timestamped and its age is on screen. Holding one
    deliberately must not become the thing that violates ADR-0011's rule against
    presenting a position nobody observed;
  - every current and every start time is `MEASURED` or labelled `ESTIMATED`.
    This whole feature is a claim about a specific module's low-power behaviour,
    so an unsourced number is the failure mode.
- **Composes with:** T-071 (dead reckoning covers the interval this opens) and
  T-077 (assistance held ready is the other half of avoiding the cold start).
- **Hardware required:** yes, for every number in it

### T-081 · Themes are installable data
- **Priority:** P2 — [OD-11](docs/research/OWNER_DECISIONS.md#od-11--themes-are-installable-and-the-layout-survives-them),
  and the owner marked it *обязательно*
- **Dependencies:** T-009 (**done** — it is the substrate), T-046 (crash-safe
  persistence), T-034 (icons must be named before they can be replaced)
- **Goal:** an ADR. A theme is **data**: colour values for the twelve roles in
  both themes, a font, an icon set. It never carries layout and never carries a
  pixel count — a theme that could set a padding could break every screen, and
  *"чтобы всё не поехало"* is exactly the requirement that it cannot.
- **Acceptance:**
  - the built-in theme cannot be uninstalled, and a theme that makes the screen
    unreadable is removable **without reading the screen**. The recovery path is
    designed first, not after somebody is locked out;
  - installing a theme is installing untrusted content that arrived over the same
    links a message does: bounded before it is read, parsed defensively, rejected
    with a sentence a person can act on;
  - whether a theme may carry executable content is answered explicitly. The
    default answer is **no**.
- **Hardware required:** no

### T-082 · A theme is validated before it is applied
- **Priority:** P2 — [OD-11](docs/research/OWNER_DECISIONS.md#od-11--themes-are-installable-and-the-layout-survives-them)
- **Dependencies:** T-081
- **Goal:** the installation gate, built out of checks that already exist.
  `contrast_ratio_centi()` in `ui/src/color.cpp` is already the arithmetic; a
  candidate palette whose text does not clear 4.5:1 on its own page is refused,
  or applied with the failure stated in words. A candidate font that cannot draw
  every codepoint in either catalogue is refused outright.
- **Why it is not optional:** the same arithmetic found two failures in the
  **owner's own** palette (DESIGN_SYSTEM §3.2) that nobody had noticed by looking.
  A stranger's palette gets the same check and no more benefit of the doubt.
- **Acceptance:** host tests with deliberately bad themes — an unreadable one, a
  font missing one codepoint, an oversized one, a truncated one.
- **Hardware required:** no

### T-085 · `touch.min.adult`: 44 dp or 48 dp
- **Priority:** P2 — a token that is already in the code and already wrong on one
  of two sources
- **Dependencies:** none
- **Goal:** decide. 44 dp comes from the general touch-target literature the
  160 dpi reference belongs to; Wear OS's own quality guideline (WO-V2) says
  **48 × 48 dp** for a wrist. On the T-Watch that is 72 px versus 79 px — 7.0 mm
  against 7.6 mm — and on a 240 px panel four extra pixels per side is a real
  layout cost.
- **Acceptance:** one number, with the reason written down, and `ChildMode`
  re-derived from it rather than left at 56.
- **Hardware required:** a finger and a panel, so **yes** for the final answer

### T-086 · Themes and packs: the format, informed by the survey
- **Priority:** P2 — supersedes the shape of T-081, which was written before the
  survey existed
- **Dependencies:** T-084 (**done**), T-081, T-082
- **Goal:** the format decision, taking the four ideas the survey says are worth
  copying: declarative and never executable; **limits that live in the format**
  rather than in a style guide (Flipper's `Duration` and `Active cooldown`,
  Wear's 15 % ambient rule); a **validator shipped with the format** and run at
  install time on the device; and Bangle.js's app-loader shape for distribution —
  a static index, no store, no server, self-hostable.
- **Acceptance:** an ADR that answers what a pack may contain, what it may never
  contain, and what the device does with one it does not like.
- **Hardware required:** no

### T-087 · Living watch faces: the passive/active model
- **Priority:** P2 — this is the *"чтобы детишкам нравилось"* feature, and the
  survey says it is a power model before it is a feature
- **Dependencies:** T-086, and the wrist-raise gesture, which needs T-060
- **Goal:** the animation model. A cheap passive loop, an expensive active
  sequence played on wrist-raise, a cooldown so it cannot re-trigger continuously,
  and a duration so one pack cannot pin itself on screen. Flipper recommends
  **1–8 fps** on a device with no battery anxiety at all, which is the number to
  argue against rather than from.
- **Also:** the memory arithmetic every platform uses takes the **union of frame
  bounding boxes**, so a small moving element is cheap and a full-screen one is
  not, however little of it changes. That shapes the format, not just the guidance.
- **Measure before designing:** what an idle animation costs on an IPS 240 × 240
  and on a 410 × 502 AMOLED are two different answers, and the AMOLED's depends
  on which pixels. `UNKNOWN`, hardware required.
- **Hardware required:** yes, for every power number

### T-088 · Save a whole track on request — the second track feature, not a mode of the first
- **Priority:** P2 —
  [OD-13](docs/research/OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature) §3
- **Dependencies:** T-065 (the storage and the simplifier are shared), T-046
- **Goal:** an application the wearer starts deliberately, which records a track
  until they stop it and keeps it to look at afterwards on a map.
- **Why this is a separate task and not a flag on T-065.** They differ in every
  dimension that matters to an implementation:

  | | T-065, the way back | T-088, saved on request |
  |---|---|---|
  | starts | by itself, on leaving familiar ground | because a person asked |
  | how the wearer is travelling | on foot only | **any** — a car is fine here |
  | ends | on return, and the track is **erased** | when the wearer stops it, and the track is **kept** |
  | when storage fills | drop the oldest, the tail is what gets you home | this is a data-loss event and the wearer is told |
  | consumer | the wearer, right now, lost | the wearer, later, on a map |

  A single mechanism with a flag would have to be right about all five at once,
  and the erase rule and the keep rule are the same code path with opposite
  requirements. That is the shape that produces a track deleted while somebody
  was relying on it.
- **The form the owner asked for:** an application, allowed to keep recording in
  the background so other applications keep working. Background here is a
  capability the platform grants, not a thread an application starts — the
  ownership question belongs in the design, not in the app.
- **Acceptance:** host tests over a recording that outlives the application
  being closed and the device sleeping; an explicit, tested behaviour when
  storage fills that never silently discards; the "was this simplified" flag
  honest end to end.
- **What must not be assumed:** that this shares the recording *rule* with
  T-065. It shares the storage, the encoding and the simplifier, and nothing
  above them.
- **Hardware required:** no for the logic; yes for anything said about what
  continuous recording costs.

### T-095 · What the day theme costs on a 400 mAh emissive board
- **Priority:** P1 — it is a default, and a default nobody costed.
- **Dependencies:** none to start; a meter to finish.
- **Why now:** the received Waveshare carries **400 mAh**
  ([WAVESHARE_BOARD_RECEIVED](docs/research/WAVESHARE_BOARD_RECEIVED.md) §1.2),
  against the T-Watch's 940, and it is the board with the emissive panel. The
  day theme's gamma-decoded emissive load is 13.9× the night theme's on the same
  pixels — `ESTIMATED` from pixel values, never measured.
- **Goal:** turn that ratio into a number with a unit. Panel current at a known
  average picture level, day theme and night theme, same screen, meter in series
  with the cell. Then the same for the Clock, which is the screen that is up
  longest.
- **Acceptance:** a measured mA figure per theme with the method written down,
  and a runtime estimate that says plainly which of its inputs are measured and
  which are not. If the answer is that the day theme is unaffordable as a
  default here, say so and let the owner decide — **this task does not get to
  change the palette**, and a recommendation dressed as a finding is worse than
  no finding.
- **What must not be assumed:** that a per-pixel estimate scales to a panel. It
  ignores the driver, the regulator's efficiency curve and whatever the CO5300
  does with its own idle modes.
- **Hardware required:** yes, and it is on the desk. `NOT EXECUTED — HARDWARE
  REQUIRED` until it is run.

### T-096 · Decide the node link on the pads that actually exist
- **Priority:** P2 — [ADR-0008](docs/adr/0008-mesh-service-providers.md), and it
  becomes urgent the moment anybody solders.
- **Dependencies:** T-072a for what the node speaks.
- **Why now:** the Waveshare's expansion row is transcribed
  ([WAVESHARE_BOARD_RECEIVED](docs/research/WAVESHARE_BOARD_RECEIVED.md) §1.5)
  and it offers exactly one uncommitted channel: `RXD`/`TXD`. `IO15` and `IO14`
  are printed as bare GPIO numbers and are the main I2C bus with six devices on
  it.
- **Goal:** decide, and write down, how an Attadipa node attaches to this board —
  UART on the pad row, or I2C as a seventh device, or USB. Then say what happens
  electrically when the node browns out or holds a line low, per option.
- **Acceptance:** an ADR amendment or a new ADR naming the transport, with the
  failure mode of each rejected option stated rather than implied. A decision
  that does not say what the *watch* does when the node misbehaves is not
  finished.
- **What must not be assumed:** that the pad row is 5 V tolerant, or that `3V3`
  can source a node's transmit current. Neither is established.
- **Hardware required:** no to decide; yes to prove.

### T-097 · Haptics on a board with no motor fitted
- **Priority:** P1 — the specification asks for haptic feedback and OD-6's
  neighbours assume it.
- **Dependencies:** none.
- **Why now:** on the received unit the `MOTOR` pads are bare and the coin-motor
  footprint is empty
  ([WAVESHARE_BOARD_RECEIVED](docs/research/WAVESHARE_BOARD_RECEIVED.md) §1.7).
  The GPIO-18 drive circuit is present and correct, so the board can drive a
  motor it does not have — and nothing in firmware can tell the difference.
- **Goal:** three separate answers, in this order. (1) Does Waveshare ship a
  motor loose in the box, and does the product listing promise one? (2) If not,
  what does `Capability::Haptics` resolve to on this board — `Unsupported`, which
  is terminal and must be stable at runtime, or something configured? (3) What do
  the screens that use haptics do when the answer is `Unsupported`, given that a
  silent no-op is the one thing a haptic cue must not be.
- **Acceptance:** the capability's value on this board decided and justified in
  the registry, with the reason in a comment that names this task; every caller
  audited for what it does without haptics; and if the value is configurable,
  the mechanism must not be an `#ifdef BOARD_*` anywhere above the BSP.
- **What must not be assumed:** that a motor can simply be soldered on later and
  the problem goes away. It can, and firmware still cannot detect it — which
  makes this a configuration question, not a probing question.
- **Hardware required:** no for the decision; yes to confirm by feel.

### T-104 · `xiaozhi-esp32`: the licence — **step 1 DONE** 2026-08-22, step 2 open
- **Priority:** P1 — it is the audio bring-up for the exact board we have,
  already written by somebody who had it working, and the licence now permits
  reading it.
- **Dependencies:** none. Step 1 is done and it was the gate.
- **Goal, what remains:** read
  `main/boards/waveshare/esp32-s3-touch-amoled-2.06/` and `main/audio/` at the
  pinned commit and write this board's audio path up as facts with
  file-and-line citations — which part is configured first, how the codec is
  clocked against the pins the schematic gives, what sample rates the board
  actually runs, and **what the second microphone is for**. One microphone is a
  microphone; two are a decision.
- **Acceptance:** a document that cites source rather than paraphrasing it, and
  that separates what is a fact about the *hardware* from what is a choice of
  *theirs*. The `esp_codec_dev` path is the one to follow, being Apache-2.0.
- **What must not be assumed:** that a permissive licence on the repository
  extends to what it depends on. It does not, and this task found out the hard
  way — see below.
- **Hardware required:** no for the reading; yes to confirm any of it.

#### What step 1 established
- **The gate is passed for the repository itself, and closed for three of its
  dependencies.** Two decisions, and they do not get the same verb — the record
  is in the [reuse ledger](docs/research/REUSE_LEDGER.md).
- **`78/xiaozhi-esp32` is verbatim MIT.** The `LICENSE` file was fetched and
  hashed rather than read off a GitHub sidebar label; no submodules, no GPL
  anywhere in the tree. It carries a board directory for **this exact board**,
  `main/boards/waveshare/esp32-s3-touch-amoled-2.06/`. So step 2 may read it, and
  may copy under MIT provided the notice is preserved.
- **Its audio-path dependencies are the finding, and it is worse than the trap we
  were watching for.** `espressif/esp-sr`, `espressif/esp_audio_codec` and
  `espressif/esp_audio_effects` are **not** MIT: they carry a field-of-use
  restriction — *"for use on all Espressif Systems products"*, and clause 3 of
  the modified licence forbids redistribution *"for use with non-Espressif
  products"*. The ledger's rule is that anything incompatible with MIT does not
  enter this repository, so as **dependencies of Attadipa** they are `REJECT`.
  Note what this is not: an MIT project calling a restricted component does not
  become restricted, and reading xiaozhi's own source is unaffected.
- **`esp_audio_codec` *is* `esp-adf`**, arriving through the component manager
  rather than as a submodule — its registry metadata gives its repository as
  `espressif/esp-adf-libs`. That is exactly the dependency this task was told to
  look for, wearing a different name.
- **The clean path exists**: `espressif/esp_codec_dev` is genuine Apache-2.0 and
  is what supplies the ES8311 / ES7210 drivers, which is the bring-up knowledge
  step 2 actually wants.
- **One question is the owner's and is filed as such** — both Attadipa targets
  *are* Espressif silicon, so the field of use would be satisfied in operation.
  Whether that is a bargain worth taking is a licence-policy call, not a research
  one. It blocks nothing in step 2.
- **Step 2 remains open**: read the board's audio path and write it up with
  file-and-line citations. Identifying the vendor's firmware is **not** a decision
  to ship a wake word.

### T-105 · Is `AAC210602A1` the speaker or a haptic actuator?
- **Priority:** P1 — it decides what `Capability::Haptics` resolves to, and
  T-097 cannot be answered underneath a wrong answer here.
- **Dependencies:** none.
- **Why now:** two readings of the same unit disagree. This repository has the
  part as the **speaker** in the back cover; a parallel reading calls it a haptic
  module and concludes the board therefore has haptics after all
  ([WAVESHARE_FLASH_LAYOUT](docs/research/WAVESHARE_FLASH_LAYOUT.md) §6). AAC
  Technologies makes both, so the marking settles nothing.
- **Goal:** trace the two solder pads. A speaker sits behind the ES8311 and its
  amplifier; a haptic actuator does not. Continuity from the pads to the codec's
  output stage answers it in one measurement.
- **Acceptance:** the [hardware matrix](docs/research/HARDWARE_MATRIX.md) row
  moves off `CONFLICTING` in one direction with the measurement recorded, and
  T-097's premise is restated against whichever answer wins.
- **Update 2026-08-22, and it shortens the job:** the `storage` partition turns
  out to hold **three MP3 background tracks**, two of them stereo, at 112–128
  kbps, played by the factory demo's `MusicPlayer`
  ([WAVESHARE_FLASH_LAYOUT](docs/research/WAVESHARE_FLASH_LAYOUT.md) §4.3). A
  board that ships 788 kB of licensed music and an app to play it has a speaker.
  Combined with the grille and the separate `P1`/`P2` motor pads, the haptic
  reading is now hard to sustain — but the trace is still what closes it.
- **What must not be assumed:** that the case grille settles it, or that the
  music does. Both are strong evidence and both are still evidence, not a trace.
  Stereo source material decoded to one transducer is still mono output.
- **Hardware required:** yes — a meter on the board.

### T-106 · Three measurements and five registers, before any cell is ordered
- **Priority:** P1 — it gates the battery decision, and every part of it is
  cheap. Nothing here needs a soldering iron.
- **Dependencies:** the research is done —
  [BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md). What is missing is
  physical, and only the owner can take it.
- **Why it is not one measurement.** The note's sizing table branches on all
  three, and each answers a different way of being wrong:
  - **M1 — closed-case clearance**, *not* the depth of the recess. Three
    plasticine balls, the cover screwed to normal torque, and the **smallest**
    of the three is the number. A cell chosen against the recess depth fits
    until the case is closed on it.
  - **M2 — the clear rectangle, the length the connector actually leaves, and
    the diagonal.** The diagonal is the one that gets forgotten: a rectangle
    that fits on paper can fail to lie flat inside a body 42.00 mm across.
  - **M3 — weigh the fitted cell**, which is the lie detector. Pouches in this
    class run 1.74–2.26 g/cm³ across four manufacturers. **6.0–6.5 g is
    consistent with 280–330 mAh; 7.5–8 g is the only mass consistent with a
    genuine 400 mAh**, and no sampled pouch reaches that density. A kitchen
    scale settles what 51 datasheets can only estimate.
- **And five registers, on the board, whenever convenient:** `0x62` (charge
  current — the one value that has never been read and cannot be quoted from
  the datasheet, because its reset value is eFuse-trimmed), `0x50`, `0x58`,
  `0x12` and `0x69`, at I²C address `0x34`.
- **And one bus scan, now a precondition rather than a footnote:** whether
  `0x6A` is free for the magnetometer retrofit ([#83](https://github.com/hleserg/Attadipa/issues/83))
  depends on which QMI8658 address mapping this unit's IMU follows —
  [HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md) marks that
  `CONFLICTING` — and the resolving scan on `SDA 15 / SCL 14` is the same one
  [WAVESHARE_BOARD_RECEIVED](docs/research/WAVESHARE_BOARD_RECEIVED.md) §3
  already lists as outstanding. Do not resolve the conflict by picking the
  likelier mapping; run the scan.
- **Acceptance:** each of M1, M2 and M3 recorded as `MEASURED` with the
  instrument named, the five register values recorded as read, the bus scan
  recorded as run with whichever address ACKed, and the sizing table in
  [BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md) resolved to one row.
  `UNKNOWN` stays `UNKNOWN` for anything not actually taken.
- **What must not be assumed:** that the sticker settles the capacity. Reading
  it was verified; what it means is exactly what is in doubt.
- **Hardware required:** yes — the board, a caliper, a scale, and one I²C read.
- **Update 2026-08-22 — PAUSED behind [#83](https://github.com/hleserg/Attadipa/issues/83).**
  The owner has ordered a magnetometer to solder into the same cavity this
  task's M1/M2 measure, so the cell is chosen after the sensor is sited, not
  before ([STATUS.md](STATUS.md), [BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md)).
  **This task's text above is left exactly as written** — the measurements
  themselves are still correct and still the ones to take — but they are not
  to be taken, and no cell is to be ordered from them, until #83 sites the
  sensor. Do not pick this task up before then, even though it is still filed
  under `## READY` rather than moved.

### T-109 · The magnetometer that is in the post, and the one measurement that chooses it
- **Priority:** P2 — nothing can start until the parts land, but what to do
  when they land is decided now, while there is time to be wrong about it
  cheaply.
- **Dependencies:** the datasheet work is done —
  [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md). What remains
  is physical.
- **What is already settled, so that nobody re-opens it:**
  - Both parts run at 3.3 V. The AK09911C is *not* a 1.8 V part; `VDD` is
    2.4–3.6 V.
  - **`CAD` goes to ground.** That puts the AKM part at `0x0C` and leaves the
    QST part at its fixed `0x0D`, so both can be on the bus at once — which is
    the only way to compare them in the same magnetic environment on the same
    wrist. `0x34`, `0x38` and `0x6A`/`0x6B` are taken; these two are free.
  - **The IMU will not read the magnetometer for us, and the reason is stronger
    than "wrong part".** QMI8658C Mag Mode names AK09915C, AK09918CZ and QMC6308
    — neither ordered part among them — but the decisive point is that `CTRL4`
    `mDEV<3:0>` has **no published encoding for any device at all**, including
    those three, and QST *deleted* the magnetometer description from the
    datasheet at Rev 0.8. So Mag Mode is undrivable from published documentation
    whichever part is fitted, and buying a listed part would not change that.
    The host reads the sensor and the host does the fusion.
    [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md) §5.1.
  - **Withdrawn, so nobody reinstates it:** an earlier draft gave a second
    "fatal" reason — that Mag Mode needs pins Mode 1 requires be tied off. That
    was an inference and it was wrong; Mode 2 is entered in firmware via `CTRL7`
    `mEN`. Whether `SDx`/`SCx` are actually tied off on this board is `UNKNOWN`
    and now merely interesting. §5.3.
- **The measurement that decides the part:** the field at the candidate mounting
  position, motor idle and motor driven. The QMC5883L is the recommendation on
  current alone — 250 µA against 2.4 mA at 100 Hz — but it saturates at ±800 µT
  where the AKM part reaches ±4900 µT, and a vibration motor magnet a few
  millimetres away is exactly the thing that closes a six-fold range advantage.
  If the QMC sits near overflow wherever it physically fits, the AKM part is not
  a fallback, it is the answer.
- **Acceptance:** both module footprints recorded as `MEASURED` with the caliper
  named; the field at the chosen position recorded with the motor in both
  states; the rotation between module frame and board frame written down for
  *this assembly* rather than inferred; overflow surfaced by the driver as a
  state an application can see, never as a clipped number passed upward.
- **What must not be assumed:** that a module which fits on the bench fits under
  a closed cover — the same trap as T-106's M1 — or that the axis arrows on a
  purple PCB survive being glued in at whatever angle it fits.
- **Hardware required:** yes. The parts are not here.

## BLOCKED

### T-010 · Board bring-up
```
BLOCKED:
Reason:         No physical board is available, and the exact variant is unknown.
Evidence:       OPEN_QUESTIONS A1, A2. The T-Watch ships with one of five radio
                chips and one of two GNSS modules; the GNSS power rail differs
                between board revisions (BLDO1 vs DC3).
Impact:         Blocks all bring-up, every power measurement, the whole
                interference matrix, and any claim that hardware works.
Possible options:
                1. Proceed on simulator and host tests only — no hardware claims.
                2. Obtain a board and record its exact variant.
                3. Write the bring-up checklist now so that day one with real
                   hardware is not spent improvising.
Recommended next action:
                Option 3 now, in parallel with option 1. Ask the project owner
                about hardware availability (A1–A3); A4 is closed, not
                outstanding (OD-14).
```

### T-011 · Interference measurement
```
BLOCKED:
Reason:         Requires physical hardware.
Evidence:       T-010.
Impact:         The coexistence layer cannot be justified or tuned. Settling
                intervals would be invented numbers, which final §26 forbids.
Possible options:
                1. Build the diagnostic tooling now, run it later.
                2. Defer entirely.
Recommended next action:
                Option 1 — the tooling is host-testable, and it is what turns a
                theory into a measurement.
```
- **The blocker changed on 2026-08-22 and is now smaller.** This task used to be
  impossible in principle: neither board has a magnetometer, so the headline
  haptics-versus-compass case had nothing to measure with. **A5 is answered** —
  the owner has ordered two magnetometer modules and is soldering one in
  ([#83](https://github.com/hleserg/Attadipa/issues/83),
  [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md)). It is now
  waiting for a part in the post, not for a device that does not exist.
- **On the Waveshare unit it stays doubly blocked**, for a second and unrelated
  reason: that unit has **no vibration motor fitted**, so there is nothing to
  interfere with the compass even once the compass exists.

---

## WAITING

### T-012 · Answers from the project owner
- **Priority:** P0
- **Waiting on:** the project owner
- **Questions:** [OPEN_QUESTIONS](docs/research/OPEN_QUESTIONS.md) A1, A2, A3,
  A6 — hardware availability and revision · which radio and GNSS variant ·
  a second mesh device · whether the node carries a magnetometer. **A5 is
  answered** — 2026-08-22, the owner has ordered a CJMCU-9911 (AK09911C) and a
  GY-271 (QMC5883L) and is soldering one in
  ([#83](https://github.com/hleserg/Attadipa/issues/83)). A6 is untouched by
  that: a node's magnetometer and a wrist's magnetometer answer different
  questions. **A4 (the regulatory region) is no longer on this
  list** — closed 2026-08-22, not by an answer but by the owner declining to
  give one: legality is his problem, not the firmware's
  ([OD-14](docs/research/OWNER_DECISIONS.md#od-14--which-region-is-the-owners-problem-not-the-firmwares)).
  No task here researches a specific jurisdiction's rules on the project's own
  initiative; [ADR-0006](docs/adr/0006-settings-and-bounded-values.md)'s
  transmit-closed-while-`Unknown` gate needs no such research to keep working.
- **Impact:** A1–A2 gate all hardware work, and A2 got sharper: of the five
  candidate radios, two cannot do LoRa at all and only one is supported by the
  pinned MeshCore ([ADR-0003](docs/adr/0003-radio-not-lora.md)), so the answer
  decides whether the watch has a local mesh path at all. A5 and A6 decide
  whether five magnetometer epics are dormant or dead, and A6 does **not** give
  the watch a compass even if the answer is yes
  ([ADR-0009](docs/adr/0009-heading.md) §3). A5's answer moves those five epics
  from *possibly dead* to *dormant with a delivery date*, and hands
  [ADR-0009](docs/adr/0009-heading.md) a second possible provider for heading —
  see [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md).
- **None of these blocks M1.**

### T-014 · Mandatory backlogs from the specification
- **Priority:** P2
- **State:** written, not started as work.
- **What:** the three mandated backlogs exist with a per-epic gate —
  [COMPANION_BACKLOG](docs/mobile/COMPANION_BACKLOG.md),
  [MAGNETOMETER_BACKLOG](docs/hardware/MAGNETOMETER_BACKLOG.md),
  [COEXISTENCE_BACKLOG](docs/hardware/COEXISTENCE_BACKLOG.md).
- **What the exercise surfaced:** two coexistence epics — haptic/magnetometer and
  audio/magnetometer interference — could not be run on either target board,
  because neither has a magnetometer, and five magnetometer epics were blocked on
  hardware that did not exist (A5).
- **Superseded 2026-08-22, and the status word has to change with it.** A5 is
  answered: the owner ordered a CJMCU-9911 and a GY-271 and is soldering one in
  ([#83](https://github.com/hleserg/Attadipa/issues/83)). Those seven epics are
  **blocked pending a part in the post**, not `NOT POSSIBLE`. The distinction the
  other files are careful about holds here too: a **stock** board still has no
  magnetometer and the firmware still has to run on one, so the epics describe a
  capability that will exist on exactly one physical device — which is a registry
  problem, not a board problem
  ([MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md) §0).
- **Startable now without hardware:** C-02 bus ownership, C-03 rail arbitration
  and C-12 diagnostic trace. The trace in particular should be finished *while*
  waiting for hardware — every blocked coexistence test needs it to produce
  anything more than an anecdote.

---

## DONE

### T-107 · Why agent runs died with no explanation — **DONE** 2026-08-22
- **The cause was not the model, the context or the turn ceiling.** It was
  `allowed_bots: ""` in `claude-agent.yml`. The hourly watchdog hands a task
  over with `gh workflow run` under the built-in `GITHUB_TOKEN`, so the
  dispatching actor is `github-actions[bot]`, and
  `anthropics/claude-code-action` refuses a non-User actor that is not on that
  list: *"Workflow initiated by non-human actor: github-actions (type: Bot)"*.
  Five seconds, no execution log, and the hand-over could only say
  `no conclusion`.
- **The autonomous queue had therefore never worked.** Every agent run that
  succeeded on this repository was started by a person commenting `@claude`;
  every run the watchdog started died before reading a file. That is what made
  it read as random — the deaths correlated with *how the task was started*,
  which nothing displayed, rather than with the task, the size or the model.
- **Found by the previous task's own output.** `failure-reason.sh` (#81, merged
  the same day) replaced "the cause is in the run log" with *"no execution log
  was written — the agent step did not get far enough to leave one"*. That
  sentence is what sent anybody to the step log instead of the model, and it is
  the whole return on that work.
- **Fixed by naming one dispatcher rather than opening the gate.** `'*'` would
  let any GitHub App drive a write-capable agent on a public repository, which
  the original comment was right about. `github-actions` is the actor of
  workflows in this repository and nothing else.
- **`queue-scan.jq` is untouched**, and the distinction matters: `claude` and
  `github-actions` are still non-listable in `ATTADIPA_TRUSTED_PRODUCERS`, so
  our own output still cannot enqueue a billable writer. Being allowed to press
  the button on an approved task is not the same as being allowed to file one.
- **`.github/tests/bot-actor-test.sh`** holds the two files in
  agreement, because each was defensible alone and only the pair was wrong —
  nothing either file's own review could have caught. It fails on the
  pre-fix state, which was checked rather than assumed.
- **T-110 carries the 500 KB reading list forward** as a concern of its own. It
  was this task's leading theory and it was wrong.
- **Then the same question was asked of the other two workflows, and the
  reviewer had it too.** `claude-pr-review.yml` admits `claude[bot]` in its
  `if:` deliberately — a blanket bot guard had been skipping the review on the
  agent's own pull requests, which are the ones it exists for — and then handed
  the action `allowed_bots: ""`. Runs `32597016812` (#95), `32596445164` (#94),
  `32595947792` (#92) and `32595273274` (#88) all died in four or five seconds
  with the identical *"Workflow initiated by non-human actor: claude (type:
  Bot)"*, so **no agent-authored pull request has ever been reviewed** — and all
  four reported **success**, because that step carries `continue-on-error`. Now
  `allowed_bots: "claude"`, and the test asserts the rule over all three agent
  workflows rather than the two instances that happened to be found.
- **`claude-ci-repair.yml` is left at `""` on purpose.** It admits no bot actor,
  so it needs to name none. It is one token change away from the same failure,
  and the test will say so the moment it grows an exemption.
### T-099 · Finish and verify the factory flash backup — **DONE** 2026-08-22
- **Verified, by the only test that settles it.** `esptool verify-flash 0x0` over
  all **33 554 432** bytes returns `Verification successful` — the device
  computes the MD5 itself, so the comparison is against the flash, not against
  another copy of the same read.
- **Three complete reads agree byte for byte**: the owner's first pass on
  Windows over native USB, and two passes here on Linux over USB/IP. All three
  hash to `2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5`.
  Chunk map, method and the per-chunk lengths are in
  [WAVESHARE_FLASH_LAYOUT](docs/research/WAVESHARE_FLASH_LAYOUT.md) §2.2.
- **The `--no-stub` fallback is the procedure**, not a workaround: the stub
  aborts at five addresses, reproducibly, ten failures out of ten predicted
  across two passes on a host sharing nothing with the first but the board.
  `--no-stub` reads them at ~65 KB/s, which is *faster* than the stub managed on
  the original host. "No-stub is unusably slow" was a fact about that host.
- **The task's own warning was right, and it caught me wearing a different hat.**
  T-099 warned that a short chunk concatenates silently into a shifted image. The
  chunks here were all full length; they were concatenated in the **wrong order**,
  by `ls -v` on hexadecimal filenames, and the resulting image failed
  `verify-flash`. I published a mismatch and a paragraph inviting suspicion of the
  owner's dump before finding my own bug. Both are retracted in §2.2. **Sort
  chunk files by numeric offset, never by name.**
- **What this unblocks:** the first flash of our own firmware is now reversible,
  which is the whole point of the task. It is the precondition
  T-104 and the bench sequence in
  [WAVESHARE_ARRIVAL](docs/research/WAVESHARE_ARRIVAL.md) §5 were waiting on.
- **The dump is not committed and never will be** — Waveshare's binaries plus
  third-party all-rights-reserved audio. It lives on the owner's machine.
- **One line of the goal is still open, and it is the owner's to close.** T-099
  asked for the image *"stored somewhere that is not the machine doing the
  flashing"*. It is not: the owner's copy and this WSL host are the same physical
  machine, and that machine is the one that will do the flashing. A second
  location is asked for in
  [#100](https://github.com/hleserg/Attadipa/issues/100). The verification —
  which is what actually made the flash reversible — does not depend on it.

### T-098 · Read the ESP32-S3 errata against revision v0.2 — **DONE** 2026-08-22
- [ESP32S3_ERRATA_V02](docs/research/ESP32S3_ERRATA_V02.md). Document identified
  by version and hash: **ESP32-S3 Series SoC Errata v1.3**, 2025-03-31, md5
  `64ffc580e78b5ab3c6c5d990e0500e38`. Completeness cross-checked against
  Espressif's own `v0-2` tag page, which lists the same eight identifiers.
- **All eight errata apply to v0.2. There is no row this chip escapes**, and
  seven of the eight say `No fix scheduled.`
- **And there is no newer revision to want.** The sheet knows exactly three
  revisions and ESP-IDF's `COMPATIBILITY.md` agrees. The single
  revision-dependent improvement — USBOTG-4289 — lands *inside* v0.2, in the
  owner's favour. So "the chip is old" is not a finding; v0.2 is the best silicon
  that exists.
- **The one with teeth for this design is CACHE-126.** Its ESP-IDF workaround
  masks **every** interrupt at `XCHAL_NMILEVEL` and **freezes the data cache**
  around the unaligned head and tail of a cache write-back. Mechanism `CERTAIN`
  from vendor source; magnitude **`UNKNOWN`, `NOT MEASURED`** — Espressif publish
  no number and nothing has run on the board. It is on the octal-PSRAM path,
  which is the path this design leans on hardest, and it is not switchable: the
  patch is gated by a hidden SoC-caps symbol with no `menuconfig` prompt.
- **The note records where its own first reading was wrong**, in §6, rather than
  silently replacing it — LCD-239's workaround was attributed to the wrong
  register, CACHE-126 was wrongly said to run on the sleep path, and one bullet
  was stated as fact when it was an inference. That section is the reason to
  trust the rest.
- Answers **D18**. Nothing transfers to the T-Watch: that board's silicon
  revision has never been read.

### T-103 · What the vendor's three images actually are — **DONE** 2026-08-22
- **Six files, not three.** `/image/image1..3.bin` and a `/music/` directory
  holding three MP3s. The earlier record came from `strings` and was incomplete.
- **The image format is settled, not inferred**: each image is exactly
  **411 652** bytes = a **12-byte header** plus **410 × 502 RGB565
  little-endian**. Header is `u32` magic `0x00001219`, `u16` width, `u16` height,
  `u32` stride (820 = width × 2). `12 + w·h·2` equals the file length exactly.
- **The byte order was settled by rendering**, which is the only thing that could
  settle it: little-endian gives coherent artwork, big-endian gives noise.
- **`tools/flash/spiffs_extract.py`** does the extraction without `mkspiffs` and
  without an ESP-IDF build — the blocker the original task named. `strings`
  recovers a SPIFFS image's names and none of its bodies.
- **Feeds T-034**, and the distinction matters: the panel's pixel format and byte
  order are facts about the hardware; the vendor's header is not, and is worth
  noticing rather than copying — it has width, height and stride but **no format
  field**, which is the field needed the moment a second format exists. Three
  full frames cost 1.18 MB.
- **Two findings outside the task's scope**, both recorded in
  [VERIFIED_FACTS](docs/research/VERIFIED_FACTS.md): the music gives T-105 a
  strong prior that `AAC210602A1` is the speaker, and the factory image carries
  third-party all-rights-reserved audio, which is a second reason the dump never
  goes near this repository.
- Extracted files and rendered PNGs are **not committed**. The extractor and the
  measurements are.


### T-102 · Documentation consistency in CI — **DONE** 2026-08-22
- `tools/docs/check_docs.py`, run by the `Documentation consistency` job.
  Four checks, each of a failure that had already happened here. **A fifth was
  added 2026-08-23** — nothing unexpected is tracked at the repository root —
  after `git add -A` swept a scraped vendor page into `main` through
  [#98](https://github.com/hleserg/Attadipa/pull/98).
- **Relative links resolve.** These documents cite each other constantly and a
  link that 404s reads exactly like one that works until somebody clicks it. The
  repository was clean at the time this landed; the point is that it stays that
  way through the next rename. Fenced code, external schemes and root-relative
  `/paths` are all handled.
- **Task IDs are unique.** Four pairs had accumulated. Two were stale open copies
  of tasks already recorded as `DONE` — T-083 and T-084 — and those copies are
  deleted, their substance already being in the `DONE` records. Two were genuine
  collisions between unrelated work: T-054 and T-039 each named a live task *and*
  a historical record. **The historical record keeps the number** — commit
  `5810e20` names T-054 in its message and history cannot be re-pointed — so the
  live tasks became **T-100** and **T-101**, each carrying a line saying why so
  nobody renumbers them back.
- **Headings inside a `<details>` block are excluded on purpose.** TASKS.md keeps
  a rejected task's original scope in one — T-073 — and that is a record, not a
  second live task. Without the exclusion this job would have failed on `main`
  from its first run, which is the specific way a hygiene check lands broken.
- **Inline code spans close.** Added after this task's own pull request shipped
  a `TASKS.md` in which a splice landed inside an inline span, truncating T-100's
  body and re-parenting its entire field list onto the next heading. Every
  heading was still unique, so the uniqueness check passed cleanly — which is the
  point. The rule is CommonMark's: a span opened by a run of N backticks closes
  at the next run of **exactly** N, scoped to the paragraph. A per-line version
  was written first and produced 61 false positives on this repository, because a
  span may wrap a soft line break and this prose does it constantly.
- **A live task has a body, and finished work is filed under `DONE`.** The span
  check above catches that splice at its cause; this catches it at its effect,
  and catches the effect however it got there — the task above a spliced heading
  loses its fields, the task below inherits a `DONE` mark in a live section, so a
  splice trips at least one of them wherever it lands. The rule is this file's
  own, stated two paragraphs into it: a live task carries priority, dependencies,
  goal, acceptance, status and tests.
- **What it found that no syntactic check would have.** Four records were sitting
  in live sections marked `DONE` — T-034, T-060, T-060a and T-084 — drift that
  predates the splice by weeks, and the same defect the #48 review established
  for T-064 and T-073. All four are moved into `## DONE` here. T-084 is worth
  naming: the bullet above says its stale open copy was deleted because the
  substance was already in the `DONE` records, and it was not — the record itself
  was in `## READY`.
- **Under `## BLOCKED` the body is the blocker, not a priority.** T-010 and T-011
  carry the `BLOCKED:` block CLAUDE.md specifies and no `**Priority:**` field,
  which is correct rather than missing. That block is written inside a fence, so
  this is the one place in the checker that reads fenced lines — everywhere else
  a `**Priority:**` inside a fence is an example and does not count as a body.
- **Mutation-tested**, and CI runs those tests before it runs the checker:
  twenty-five cases in `tools/docs/test_check_docs.py`, thirteen of which assert
  the checker does *not* fire where firing would be wrong. One reproduces the
  splice defect above verbatim and asserts the span check catches what the
  uniqueness check cannot; another asserts the body check catches the same splice
  from the other side.
- Invoked through `python3`, never as `./check_docs.py` — the working copies this
  repository is edited from report `core.filemode=false`, so an executable bit
  set locally never reaches a commit.

### T-034 · Image asset pipeline — **DONE** 2026-08-22
- `ui/assets/source/` → `tools/assets/` → `ui/assets/generated/`, exactly the
  three directories final §45 names, with LVGL v9.5.0's `LVGLImage.py` vendored
  unmodified at `tools/assets/vendor/` and pinned by hash.
- **Deterministic**, verified rather than assumed: two runs, byte-compared.
- **The staleness gate covers the converter as well as the art.** An encoder
  that changes its output *is* the asset changing, so its SHA-256 is inside
  `INPUTS.sha256` and a bump fails `ui_images_are_current` until the tree is
  regenerated.
- **Three refusals, each with a test that triggers it:** a source over 512 px
  (the 1440-pixel concept sheets, §41); a source under `docs/` or `pics/`; and a
  pixel size with no drawing behind it — which is final §86 made mechanical
  rather than aspirational, because the pipeline **never resamples one size into
  another** and `icon()` returns `nullptr` rather than the nearest thing it has.
- **Proved with three icons** — `mesh`, `position`, `warning` — authored at 33,
  39 and 47 px with per-size geometry in `tools/assets/icon_drawings.py`. Nine
  A8 masks, **14 457 B** of `.rodata`, reported per asset rather than estimated.
- **Assets are named by pixels, never by board.** `icon.size.lg` at 261 dpi and
  `icon.size.md` at 315 dpi are both 39 px and share one file; a test asserts the
  two lookups return the same pointer. Four tokens × two densities is seven
  distinct sizes, and the manifest names the three it generates rather than
  taking the cross-product, because a mask costs its pixel count in flash.
- Review sheet: `docs/ui/specimens/sheet-icons.png`, day and night, 1:1.
  DESIGN_SYSTEM gained §7.1 and §7.2; RESOURCE_BUDGET gained the numbers; the
  reuse ledger records `USE AS-IS` for the vendored converter.
- **Not done, and split out rather than quietly dropped:** the mascot — T-034a.
- **Not measured on hardware.** The byte counts are `CALCULATED` from the
  format; `idf.py size` is the only thing that settles cost after alignment.

### T-060 · What each IMU actually does about steps — **DONE** 2026-08-22
- [PEDOMETER_PARTS](docs/research/PEDOMETER_PARTS.md), and four entries in
  [VERIFIED_FACTS](docs/research/VERIFIED_FACTS.md). Read from the datasheets,
  Bosch's own reference driver and LilyGo's board support, in that order.
- **BMA423: yes, it counts steps** — a 32-bit counter at `0x1E`–`0x21`. **And
  its datasheet does not say how.** All four registers carry one line:
  *"Application note – Wearable feature set"*. Every behavioural question — power
  mode, required ODR, reset survival, and whether it counts while the SoC sleeps
  — is in `BST-MAS-AN032`, which returned HTTP 403. **T-060a.**
- **The feature is a 6 144-byte blob the host uploads at every boot**, with a
  mandatory **150 ms** wait and a status register that must read
  `ASIC_INITIALIZED`. Whether a soft reset drops it is `UNKNOWN`, and if it does,
  every reset is a hole in the day's total.
- **The watermark is 10 bits and 0 does not mean "every step"** — it selects the
  separate step-detector interrupt. LilyGo's own board support sets it to 1.
  *(**Corrected by T-060a:** the field carries an implicit ×20, so that is an
  interrupt every 20 steps, not every step.)*
- **One interrupt line, already shared six ways.** INT2 is bonded out but not
  routed on the T-Watch, and LilyGo maps step counter, any-motion, no-motion,
  activity, tilt and wake-up all to INT1. A design needing a private interrupt
  for steps does not fit this board.
- **QMI8658: it depends which part, and we do not know which.** The **C** variant
  documents a full pedometer — 24-bit count at `0x5A`–`0x5C`, `CTRL8.Pedo_EN`,
  two CTRL9 commands, eight tunable parameters. **QMI8658A Rev A documented the
  identical feature; Rev D has deleted it** — feature list, chapter and registers
  alike, with no deprecation note. `HARDWARE_MATRIX` records the board's IMU as
  *"QMI8658 / QMI8658C"* and the vendor BSP does not touch the IMU, so there is
  no code to read the answer out of. **This is the ADR-0003 pattern in a second
  subsystem.**
- **Two findings that change what a step count *means*:** the QMI8658C
  retroactively counts steps it had discarded once a walk is confirmed
  (`ped_time_cnt_entry`), and updates its registers only every N steps
  (`ped_sig_count`) — **a read is stale by design**. A step count is an estimate
  produced by somebody else's filter, and ADR-0011's language about a position
  applies to it unchanged.
- **Power:** QMI8658C 30/35/42/55 µA at 3/11/21/128 Hz low-power; BMA423 13 µA
  at 50 Hz. The Waveshare board pays **at least** three times as much — the two
  figures are at different ODRs and matching them widens the gap, PEDOMETER_PARTS §2.4 —
  before its variant question is settled. Vendor typicals, **not** measurements.
- **No hardware involved.** `NOT EXECUTED — HARDWARE REQUIRED`.

### T-060a · Read the Bosch application note the datasheet points at — **DONE** 2026-08-22
- **Answered without the application note.** Bosch's site returned **HTTP 403**
  a third time, and Mouser, LCSC, Octopart and micro-semiconductor mirror only
  revision 2.0 or a product flyer. The material turned out not to need it:
  **the chapter revision 2.0 deletes is still printed in revision 1.1.**
- **BMA423 Data Sheet revision 1.1, `BST-BMA423-DS000-01`, May 2019** — pp.
  31–37 — carries the full *"Step Detector / Step Counter"* chapter, the
  *"Minimum Bandwidth Settings"* section, the phone/wrist preset tables and the
  per-field configuration list. Revision 1.0 (Aug 2017) is byte-identical there.
  Revision 2.0 (Aug 2019) replaced it all with a pointer and moved from document
  series `DS000` to `DS004`. Retrieved from the Watchy project's mirror; SHA-256
  recorded in [PEDOMETER_PARTS §1.2](docs/research/PEDOMETER_PARTS.md).
- **Four of the five questions are answered `SUPPORTED`:**
  - **counts while the host sleeps** — the sensor duty-cycles itself and feeds
    the feature engine at 50 Hz; register contents are retained in every power
    configuration. What is left is a *board* question about the rail, not a
    sensor one;
  - **required configuration** — features consume samples at 50 Hz. Performance
    mode: any ODR. Low-power mode: **minimum 50 Hz**, 200 Hz only for tap, and a
    violation sets `INTERNAL_STATUS.odr_50hz_error` rather than failing quietly;
  - **feature current** — the budget line is the 50 Hz low-power figure,
    **13–14 µA `ESTIMATED`**. Not 42 µA, not 150 µA;
  - **soft reset** — the blob does **not** survive. *"Initialization has to be
    performed as well after every POR or soft reset."*
- **One stays `UNKNOWN`:** behaviour at the 32-bit boundary. Not in revision 1.1
  either. **T-060b**, and it changes nothing — the firmware treats any decrease
  as reset-or-wrap regardless.
- **And one earlier claim was wrong.** The 10-bit watermark field *"holds
  implicitly a 20x factor"*, and Bosch's driver writes the argument raw — so
  LilyGo's `setStepCounterWatermark(1)` is an interrupt every **20** steps, not
  every step. Corrected in both documents, marked as a correction.
- **Two things nobody asked for:** the step algorithm's **wrist preset is
  already the default**, so T-061 writes none of the 25 parameters; and axis
  remapping applies **only** to the feature engine, never to `DATA_0`–`DATA_13`
  or the FIFO, so a driver that remaps once has got one of the two wrong.
- **This was a research task.** No code came out of it.

### T-084 · Deep research: design customisation on wearables — **DONE** 2026-08-22
- [WEARABLE_CUSTOMISATION](docs/research/WEARABLE_CUSTOMISATION.md). Eighteen
  sources, read and dated. Findings that changed the plan: **every platform that
  shipped executable watch faces has moved away from them and none has moved
  back**; Wear OS publishes the only hard numbers anybody publishes (15 % of
  pixels lit in ambient, 10 MB ambient / 100 MB interactive assets, 12 sp
  essential text, **48 dp touch targets**); Flipper's passive/active split is the
  power model and the delight in one mechanism, with wrist-raise as the trigger
  the owner had already named; and **no platform validates that a user-installed
  face can be read** — they ship system-level overrides instead, which is a gap
  Attadipa can fill for free because the contrast arithmetic already exists.
- Filed out of it: T-085, T-086, T-087. And one finding against existing code:
  `touch.min.adult` is 44 dp and Wear OS requires 48.
- **Original brief, kept:** *"Прям нормальный дип ресерч. А по результатам уже
  назначишь задание себе че делать че не делать."*

### T-072 · What a vanilla MeshCore node actually exposes — **DONE** 2026-08-22
- §1 of [COMPANION_AND_POSITION_SOURCES](docs/research/COMPANION_AND_POSITION_SOURCES.md)
  is answered, and the detail it summarises is
  [MESHCORE_COMPANION_PROTOCOL](docs/research/MESHCORE_COMPANION_PROTOCOL.md) —
  transports, framing, the whole command set, the three position scalings, and a
  provenance section saying which claims were verified twice and which once.
- **LAN exists**, which is what OD-7 turned on: Wi-Fi/TCP and Ethernet/TCP, both
  port 5000 by default, one client at a time. That makes a host-side client the
  cheapest possible bring-up.
- **176 bytes is the frame budget** and it cannot be raised by a build flag.
- **The finding that outranks the rest:** a position from a vanilla node carries
  **no fix flag, no satellite count, no timestamp and no HDOP**, and `node_lat`
  is one slot shared by the GNSS loop, saved prefs and the client app. A receiver
  cannot tell a live fix from a stale one from a hand-typed coordinate. That is a
  direct input to [ADR-0011](docs/adr/0011-gnss-integrity.md), OD-8 and OD-10.
- Reuse-ledger records added for both the client (`REIMPLEMENT`) and the
  Meshtastic gate (`REJECT`).
- **Read from source, never observed.** `NOT EXECUTED — HARDWARE REQUIRED` —
  see T-072a.

### T-064 · Beacon profiles and the slot scheduler — **REJECTED**, owner decision 2026-08-22
- **Outcome:** the watch does not emulate a smart tag, in any ecosystem.
  [OD-13](docs/research/OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature),
  answering A7 on [#33](https://github.com/hleserg/Attadipa/issues/33): *"Не
  делаем. Ни Apple, ни какую-либо ещё."*
- **Why, and the order matters.** The research found the feature expensive
  before it found it unwanted, and the decision is the second one. Two of the
  three ecosystems are shut before the radio is involved — Google needs
  registration, an email allowlist and third-party certification, and its only
  readable implementation is licensed for Nordic silicon; Samsung's SDK ships
  for no Espressif part. Apple is reachable and costs an Apple ID bootstrapped
  on Apple hardware, a self-hosted endpoint and, for anything a person would
  recognise as Find My, MFi — which excludes individuals. **None of that is the
  reason.** The owner decided the feature is not wanted, which is a product
  decision and outranks the obstacles.
- **What still answers the need:** T-063 — the companion phone remembers where
  it last saw the watch over BLE. No account, no other company's identifier, no
  network, and it works with the companion this project already specifies.
- **What the research keeps, because it is about the device and not the
  feature:** DULT, rotation intervals and the 2022 fast-rotation evasion are
  still live input to T-069 and T-070. §1 of
  [TAGS_TRACKS_RECKONING](docs/research/TAGS_TRACKS_RECKONING.md) is not
  obsolete; only this task is.
- **If this is ever revisited:** nothing in the ecosystems changed the answer,
  so nothing in them would change it back. It is one decision to reverse.


### T-073 · Meshtastic as a companion — **REJECTED**, owner decision 2026-08-22
- **Outcome:** not supported. [OD-12](docs/research/OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence),
  from [#41](https://github.com/hleserg/Attadipa/issues/41).
- **Why:** the licence gate closed — `meshtastic/protobufs` is GPL-3.0 in its own
  repository with no linking exception, so generating from those `.proto` files
  and linking them would make an MIT firmware a derivative work. That made the
  cheap path impossible; the *decision* is that the feature is not worth the
  expensive one. A real clean-room is months and is done honestly or not at all.
- **What still answers the need:** MeshCore, MIT. OD-7 asked for a companion for
  people who will not build our node, and MeshCore is the remaining candidate
  whose licence permits one. **T-072 has since answered how much work that client
  is** (2026-08-22): §1 of
  [COMPANION_AND_POSITION_SOURCES](docs/research/COMPANION_AND_POSITION_SOURCES.md)
  is answered on every row, with the detail in
  [MESHCORE_COMPANION_PROTOCOL](docs/research/MESHCORE_COMPANION_PROTOCOL.md) —
  58 commands, a 176-byte frame budget that no build flag can raise, and a
  Wi-Fi/Ethernet TCP transport that makes a host-side client the cheapest
  bring-up there is. It is a real but bounded amount of work. **The rejection
  here never depended on that number and does not change now that it exists** —
  it rests on the licence gate and the cost of a clean-room, neither of which
  T-072 touched.
- **If this is ever revisited:** the licence question is answered and recorded.
  Only the product decision would need to change.

<details>
<summary>Original scope, kept for the record</summary>

### T-073 · Meshtastic as a companion — the licence is the gate
- **Priority:** P2 — [OD-7](docs/research/OWNER_DECISIONS.md#od-7--the-companion-is-any-node-not-only-ours)
- **Dependencies:** none, but pointless before T-072 establishes the shape a
  companion client takes here
- **Goal:** answer one question before any other: **are Meshtastic's protocol
  definitions licensed separately from its GPL-3.0 firmware?** Then, only if the
  answer permits, §2 of the research file.
- **Acceptance:** the licence answer cited from the `protobufs` repository's own
  `LICENSE` file, not inferred from the firmware's. If it does not permit a
  client, the deliverable is a `BLOCKED:` with options, not a client written
  carefully.
- **Hardware required:** no

</details>


### T-009 · Design tokens in code — **DONE** 2026-08-22
- `ui/` is the code half of [DESIGN_SYSTEM](docs/ui/DESIGN_SYSTEM.md): a `Dp`
  type against a 160 dpi reference, twelve semantic colour roles across two
  themes, and the spacing, radius, motion, size, elevation, typography-role,
  haptic and sound-category scales. The library links `attadipa_headers` and
  deliberately **not** `attadipa_platform` — a screen asks for `space.md` and
  only the composition root knows which panel answered.
- **Acceptance met.** `tools/ui/check_raw_values.py` refuses a colour, a pixel
  count or a duration written as a number under `ui/`, `sim/` or `apps/`, with
  two files exempted for holding the palette and the scale; `tools/ui/selftest.py`
  proves the checker rejects seven real mistakes and accepts seven correct
  lines. `sim/boot_screen.cpp` no longer contains a hex colour or a raw padding.
- **Both themes are now switchable without a rebuild** — `T` at runtime,
  `--theme day|night` for CI — for the same reason the locale is: a reviewer who
  must rebuild to see the second one checks the first.
- **Two measured findings, neither of them a proposal to change the palette.**
  Every day accent is under 3:1 against the brightest background it will sit on
  (Attadipa Orange 2.19, Glow Amber 1.44, Meadow Green 2.81, Sky Teal 2.15), so
  on the day theme an accent is emphasis and the meaning is in the icon and the
  word. And `color.text.muted` clears the threshold on the page (5.62) and on a
  surface (4.95) and then fails on a **raised** card at 4.44 — six hundredths
  under 4.5:1, on the most ordinary thing the system draws. Both are pinned in
  `tests/test_ui_tokens.cpp`, both are tabulated in DESIGN_SYSTEM §3.2, and both
  break a test if the palette moves. The brand-art-versus-§42 conflict once
  recorded as open question A7 is resolved — see
  [OWNER_DECISIONS.md](docs/research/OWNER_DECISIONS.md) OD-15.
- **Still hardware-blocked, as it always was:** final §55 forbids preserving a
  concept-board value that fails on the real display. Every number in `ui/` is
  **PROPOSED**; none has been shown on a panel. `color.danger` stays UNKNOWN.
- **Mutation-tested**: five mutants — a background falling through to day, a
  shrunken touch target, `radius.pill` resolved as a length, a hairline rounding
  away, and contrast computed from a channel average instead of WCAG luminance.
  All five red. The fourth was green on the first attempt and the test was wrong,
  not the code: nothing exercised the guarantee below 80 dpi where it bites.

### T-083 · No box characters in any build — **DONE** 2026-08-22
- The owner saw a `□` in a screenshot and asked the obvious question. It was
  real: the build drew with LVGL's stock Montserrat, generated from
  `-r 0x20-0x7F,0xB0,0x2022`, so `×` (U+00D7) rendered as a box — and so did all
  six Cyrillic codepoints in the **English** catalogue's own language names.
- `assets/fonts/` now holds four generated subsets — 14, 16, 20 and 28 px, 4 bpp
  — covering all 181 codepoints in `tools/font/charset.py`. They are committed
  rather than generated during the build, for the reason the l10n catalogue is:
  otherwise Node.js sits between a contributor and a green build.
- **Not a typeface decision.** Montserrat is used because LVGL already ships it
  under OFL-1.1 at the pinned revision and because it covers the whole charset.
  D16 and final §51 are untouched: no candidate has been checked for licence,
  coverage, legibility at real pixel size and generated flash size.
- **The warning became a failure.** `report_undrawable_glyphs()` used to print
  seven codepoints and continue, because the situation was unfixable. It is
  fixable now, so the simulator exits non-zero — and it checks **both**
  catalogues rather than whichever locale the reviewer started in.
- **Measured:** 13.0 / 15.2 / 19.4 / 31.3 kB of `.rodata`, 78.9 kB for all four,
  at `-Os` on the host compiler. `ESTIMATED` for the target until
  `tools/font/measure.py` is run with the xtensa toolchain.
- **Mutation-tested:** adding a line to `charset.py` turns `ui_fonts_are_current`
  red; putting a Latin-only font back turns the simulator run red.

### T-059 · The trust state, tested as sequences — **DONE**
- **Why sequences:** the detectors that matter are rate detectors, and a rate
  needs two epochs. A suite of single-observation tests passes cheerfully while
  every one of them is switched off — which is exactly how the interval bug
  survived being written, with `dt` read after the previous timestamp had been
  overwritten so every interval was zero.
- **Mutation-checked** rather than merely green: re-introducing that bug turns
  three checks red; treating an `Unknown` spoofing verdict as an all-clear turns
  two red (OD-5 §2); granting recovery without the hold, two; collapsing the
  hysteresis band to one threshold, one.
- **Also pinned:** `MotionEvidence{known=false}` is not evidence of stillness; an
  absent last-trusted position reads as no answer rather than as certainty; the
  transition log is bounded and reports how much it dropped.
- **Tests:** `tests/test_trust.cpp`.
- **Note:** its commit is prefixed `T-053`, which was already taken by the
  simulator task above. The number here is the correct one; the commit is left
  alone rather than rewritten.

### T-058 · The diagnostics snapshot, tested structurally — **DONE**
- **What it proved:** the snapshot survives a `memcpy` from a crash handler that
  has no allocator (asserted statically *and* exercised), is 384 bytes against a
  1 KiB bound so it can live in RTC memory beside everything else that wants to
  survive a deep sleep, carries no serializer — §14, core is not tied to JSON —
  and defaults every unread value to absent rather than to zero.
- **Tests:** `tests/test_diagnostics.cpp`. Host only; nothing was sampled.

### T-057 · The replayable navigation rig — **DONE**
- **Why it exists:** the interesting GNSS failures cannot be staged. A detector
  for an event nobody can produce on demand is one that gets written once and
  never verified again.
- **What landed:** `tests/replay/` — a strict fixture reader, a deterministic
  runner, twelve traces, and `tests/test_replay_rig.cpp`, which is the part that
  matters: it feeds the runner a deliberately-wrong fixture and demands three
  mismatches, ten malformed fixtures and demands each be refused with a reason,
  and a missing file, which is a failure and never a skip. CMake refuses to
  configure if the scenario glob matches fewer than ten files.
- **Reuse:** `INSPIRE ARCHITECTURE` from gpsd's regression framework — see
  [REUSE_LEDGER](docs/research/REUSE_LEDGER.md).
- **Feeds:** the simulator half of T-053, which the traces specify.

### T-056 · Position, validity and integer distance, tested — **DONE**
- **What it pinned:** freshness is decided before quality; `TimeOnly` is `NoFix`
  rather than a bad position; a coordinate off the globe lands in `NoFix` rather
  than being clamped into something plausible; an unasked receiver reports
  `Unknown` for jamming and spoofing and never `None` (OD-5 §2).
- **Distance:** tolerances stated as percentages of a hand-computed answer rather
  than borrowed from the implementation. A degree of longitude shrinks with the
  cosine of the latitude, and the antimeridian is 111 m rather than forty
  thousand kilometres.
- **Tests:** `tests/test_position.cpp`.

### T-055 · The two power state machines, tested exhaustively — **DONE**
- **Method:** every `(from, to)` pair in both tables, because a suite that only
  walks the legal paths passes against a `transition_is_legal` that returns true
  for everything.
- **Found two real defects:** `next_state()` proposed `Off → Backup` when the
  device slept with the receiver already off — current spent holding a domain
  with nothing in it; and `start_kind()` read *having* a backup domain as
  evidence the domain had been *powered*, reporting a warm start where the truth
  was cold. `GnssContext` gained `backup_retained`, the fact that was missing.
- **Also pinned:** no wake source that exists only while the radio is powered may
  be armed in `DeepSleep` — the rule the MeshCore review found broken upstream.
- **Tests:** `tests/test_power.cpp`.

### T-054 · The transport, tested against the brief and against upstream — **DONE**
- **First half:** the owner's §6 list, one function per item — fragmented input,
  several frames in one read, partial writes, a full queue, a disconnect
  mid-frame, a reconnect, a large payload, a malformed frame — so coverage
  against the brief can be read rather than asserted.
- **Second half:** the defects
  [the MeshCore review](docs/upstream/meshcore-1.17-review.md) verified at source
  in the upstream serial transport, held against our own code. An over-long frame
  is refused rather than truncated and delivered as complete; the checksum is
  pinned against published vectors; `Attached` and `Ready` are separate phases.
- **Tests:** `tests/test_link.cpp`.

### T-042 · Owner amendment: GNSS integrity and receiver-native protection — **DONE**
- **Closed:** 2026-08-21
- **Scope, and it is deliberately small:** the owner's §15 forbids building the
  navigation stack now. This task did the eight things §15 *does* list, and
  stopped.
- **What was delivered:**
  - the amendment recorded as
    [OD-5](docs/research/OWNER_DECISIONS.md#od-5--gnss-integrity-and-the-receivers-own-protection-comes-first);
  - [ADR-0011](docs/adr/0011-gnss-integrity.md) — eight rules: the observation
    keeps both a normalized form and the receiver's native values; ten state
    axes that may not be collapsed; the receiver capability descriptor and where
    it may **not** be read; differential corrections as a provider capability;
    trust as a state with hysteresis, weighted evidence, reason codes and a
    bounded transition log; the receiver's verdict as the strongest input rather
    than the truth; a bounded, replayable trace before any field testing; and a
    list of what is not being built.
  - T-051, T-052 and T-053 filed.
- **The RTCM assumption:** it was never written down here. A grep of every ADR,
  architecture document, research file, header and source found `RTCM` in none
  of them, and the specification in force does not mention it either. So the
  correction is a *fence*, stated in ADR-0011 §4 before the path was worn, and
  the fact that the MIA-M10Q rejects corrections is recorded as the **owner's
  claim, to be confirmed** in T-051 — CLAUDE.md's rule about technical claims
  applies to the amendment as much as to the specification.
- **What did not change:** no code. No GNSS driver, no `LocationService`, no
  observation type exists yet, which is exactly why the amendment was cheap to
  absorb and why absorbing it now was the point.
- **Hardware required:** no. Every descriptor entry starts `UNKNOWN` and stays
  there until a primary source or a fitted module says otherwise.


### T-041 · Owner amendment: MeshCore 1.17 upstream review — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:**
  [`docs/upstream/meshcore-1.17-review.md`](docs/upstream/meshcore-1.17-review.md)
  — v1.16.0 → v1.17.1 and `dev` read at source, all thirteen owner-named PRs and
  issues read through the GitHub API, and a status of
  `adopt / adapt / monitor / reject` against every item.
- **The finding that shaped the rest:** **ten of the thirteen owner-named pull
  requests are still open.** Only #3049 (multi-interface companion) and #3137
  (FEM gain persistence) are merged; #2734 is an *issue* already fixed by merged
  #3006. So most of what the amendment names is a *proposal* rather than
  shipped code, which is exactly the distinction the owner's §3 asks for and the
  reason nothing here is proposed for porting.
- **Two defects confirmed by reading the shipped tree, not by trusting a report:**
  - the **FEM/LNA regression** — `e2aa7b98` added `radio_fem_rxgain = 1` to the
    companion (v1.16.0 had no FEM pref at all), then #3203 `#if 0`'d the pref
    out. On a Heltec V4.3 running 1.17.1 the external LNA is on and cannot be
    turned off. Open issues #3010 and #3232 report the noise floor going
    −115 → −95 dB and −108 → −86 dB. Released, unfixed.
  - the **V4-R8 hibernate defect** — `powerOff()` is literally
    `enterDeepSleep(0)`, which leaves the FEM in RX and arms EXT1 on
    `P_LORA_DIO_1`. "Off" ends at the next received packet (#3165, fix #3168
    open).
  - and the **USB transport** — `isConnected()` returns `true` unconditionally
    *("no way of knowing, so assume yes")*, `isWriteBusy()` returns `false`
    unconditionally, and an over-long frame is truncated to `MAX_FRAME_SIZE` and
    delivered as if complete. The same codebase gets it right on BLE, with
    bounded queues and logged overflow — which is what makes it a lesson rather
    than a limitation.
- **What Attadipa takes:** the two-clock separation, the JSON migration that does
  not destroy its source, the preamble-detect LBT scheme with both watchdog
  deadlines, BLE's queue discipline, and the battery rules (never sample during
  transmit; flush on every shutdown path). **What it does not take:** any FEM
  default, any unmerged code, and hardware CAD, which upstream still ships off.
- **Filed as a result:** T-043 (multi-interface node link), T-044 (framing),
  T-045 (`PowerState`), T-046 (crash-safe persistence), T-047 (two clocks),
  T-048 (crypto/RNG seam), T-049 (front-end as a board capability), T-050 (the
  adapter boundary).
- **Hardware required:** no — and **no Heltec board of any revision is in this
  project's hands**, so every upstream measurement quoted in the document is
  attributed to upstream and Attadipa's own status for all of it stays
  `NOT EXECUTED — HARDWARE REQUIRED`.

### T-033 · Localization: `tr()`, catalogues, and the checks that guard them — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:** [ADR-0010](docs/adr/0010-localization.md) in code.
  `l10n/strings.toml` is the single source of truth; a Python generator emits a
  `StringId` enum, a separate `PluralId` enum and parallel per-locale tables,
  and the generated files are **committed** so the C++ build needs no Python.
  A new `attadipa_l10n` library sits beside core and is linked by apps and the
  simulator — **not** by core, and that is enforced rather than reviewed.
- **Acceptance, item by item:**
  - *a screen with no user-facing literal* — the simulator's diagnostic screen
    has none. Its rows are spelled with the `to_string()` of enums from core and
    platform, which are diagnostic identifiers rather than product text, and
    that distinction is written down in `l10n/strings.toml`.
  - *switches at runtime without a reboot* — `--locale en|ru`, and `L` toggles
    it live; the screen is rebuilt by the locale-changed handler.
  - *CI fails on a missing key, a duplicate key, or a glyph the font cannot
    draw* — three `ctest` entries, so a local run and CI enforce the same rule.
  - *the Russian plural vector* — 0, 1, 2, 5, 11, 21, 101, 111, 1001 assert
    **categories** rather than rendered strings, plus a sweep of every remainder
    class proving `other` is unreachable in Russian, which is what lets the
    catalogue format reject `ru.other`.
- **Beyond the acceptance list**, because running it found them:
  - a **fourth** generator check — placeholders must match across locales.
    `%u` in English and `%s` in Russian is undefined behaviour at the `snprintf`
    call that no compiler warning can reach.
  - a **selftest**: eight deliberate mistakes, each required to be rejected *for
    its own reason*. The `WILL_FAIL` lesson, applied before it could bite again.
  - the second **boundary test**, pointing the other way: a fixture that
    compiles against apps and must not compile against core. Proved by
    temporarily linking `attadipa_l10n` into core and watching it fail.
- **The finding:** LVGL ships no font with Cyrillic — Montserrat's own header
  says `-r 0x20-0x7F,0xB0,0x2022`. The simulator therefore **cannot draw the
  Russian catalogue**: 26 codepoints in `ru`, and 7 in `en`, because a language
  is named in itself and `Русский` is in the English catalogue too. The
  simulator names the codepoints rather than rendering boxes. This is ADR-0010
  §1's argument arriving on schedule, and it closes only when the font pipeline
  output is linked in — D16 and T-034.
- **Tests:** 10 host, 12 with the simulator. All pass.
- **Hardware required:** no.

### T-032 · Pin LVGL, and the font toolchain that comes with it — **DONE**
- **Closed:** 2026-08-21
- **What was delivered:** LVGL v9.5.0 pinned at `85aa60d` with the commit
  verified after the clone, and the font toolchain pinned and *measured* rather
  than assumed. `lv_font_conv` **1.5.3**, MIT — read from the tarball's own
  `LICENSE`, not from the manifest, along with all ten bundled dependencies
  (one of which is Python-2.0, not MIT, and is recorded as such). Inter and
  Nunito Sans recorded under OFL 1.1, checked from the `OFL.txt` beside each
  font file.
- **The measurement:** 181 codepoints in 18 ranges, defined once in
  [`tools/font/charset.py`](tools/font/charset.py) so the font build and the
  localization check cannot disagree. Generated at seven sizes × three bit
  depths × compressed and raw, compiled with `xtensa-esp32s3-elf-gcc 14.2.0` at
  `-Os`, and read as `.rodata` — 84 measurements in
  [`docs/research/font-sizes.csv`](docs/research/font-sizes.csv), written up in
  [FONT_MEASUREMENTS](docs/research/FONT_MEASUREMENTS.md).
- **What running it found, that reading about it would not have:**
  - **Nunito Sans has no arrows** (U+2190–U+2193). `lv_font_conv` refuses the
    range rather than substituting. Opened as D16, because it turns a font
    preference into a decision about where arrows come from.
  - **Both families ship as variable fonts only**, and the converter takes the
    *default* instance — which for Nunito Sans is **ExtraLight 200**. Converting
    the downloaded file silently produces a font nobody chose.
  - **Instancing Inter destroys its kerning**: 1 012 B of kern data before the
    `fontTools` round-trip at its own default weight, exactly zero after, and
    `optimize=False` does not help. So the tool now copies a font unchanged when
    the requested location is already the default, and says so.
  - **bpp 1 ignores compression entirely** — identical bytes with and without,
    at every size, for both fonts.
- **Legibility** checked at 14/16/20/28 px through `lv_font_conv`'s own `dump`
  rasterisation, in **both themes** —
  [`docs/ui/specimens/`](docs/ui/specimens/). Cyrillic including Ё is legible at
  14 px at bpp 4 in both families.
- **Deliberately not closed by this task:** render performance (D17, final §51
  asks for it and it needs timed frames), and which font (D16, a design decision
  that is the owner's).

---


### T-008 · Simulator skeleton with both geometries — 2026-08-21
- **Priority:** P0
- **Dependencies:** T-032 (LVGL pin — done)
- **Goal:** a desktop window that renders LVGL at 240 × 240 and 410 × 502, mouse
  as touch, keyboard as buttons. The simulator is a first-class target
  (final §57), not a convenience.
- **Acceptance:** both presets run; switching between them needs no rebuild;
  the build is part of CI. **Met.** `attadipa_sim --board <id>` selects the
  geometry at runtime; `--radio <chip>` fits any of the five T-Watch radios
  without recompiling, which is the same requirement one layer down.
- **Implementation status:** **done.** `sim/` holds the composition root, the
  option parser, a diagnostic boot screen and a dependency-free PNG writer.
  LVGL configuration is `sim/lv_conf_simulator.h`, generated once from the
  v9.5.0 template with every edit recorded in its header.
- **Tests:** `ctest` runs the simulator headless at both geometries under
  `SDL_VIDEODRIVER=dummy`, and each run writes a screenshot that the test
  requires to exist. CI has a second job that installs SDL2, builds with
  `-DATTADIPA_BUILD_SIMULATOR=ON` and uploads the screenshots as artefacts.
  **OBSERVED** on the development host **and in CI** — run `32462413273`,
  2026-08-21, on a runner with no LVGL and a cold cache: clone 22.8 s, commit
  verified, build, 6/6 tests, both screenshots uploaded, whole job 2 min 2 s.
  That is the from-scratch path proven, not the incremental one.
- **Hardware required:** no. Nothing here touched a bus and nothing here is
  evidence about a board.
- **What it also settled**, because the first CMake file was the last cheap
  moment to settle it: the target graph. `attadipa_platform` → `attadipa_core` →
  `attadipa_apps`, with platform linked PRIVATE into core, and two tests that
  compile one fixture against each of the two libraries to prove an application
  still cannot include a hardware header
  ([ADR-0007](docs/adr/0007-two-capability-layers.md) §5).

### T-039 · M0.5 — reconcile with the final master prompt — 2026-08-21
- All eight §75 P0 items re-checked, all eight found still present, all eight
  closed. Record: [RECONCILIATION](docs/research/RECONCILIATION_2026-08-21.md).
- Old master prompt and addendum marked superseded; the final prompt is in the
  repository at [`docs/master-prompt-final.md`](docs/master-prompt-final.md);
  the three owner design references are in
  [`docs/ui/reference/`](docs/ui/reference/README.md), hashed.
- Five ADRs written: 0003 radio · 0007 two capability layers · 0008 mesh
  providers · 0009 heading · 0010 localization. Three earlier ADRs accepted,
  one superseded, one made explicitly provisional.
- One further P0-grade correction the review did not list: *"ownership means
  initialises it"* is too strong (final §32), and the ownership tables were built
  on it.

### T-006 · Read MeshCore upstream — 2026-08-21
- M1–M9 answered from source at `d92964352441e53b93e8667b802e04f6e072b39e`,
  with file and line citations. Frame format, crypto, threading and radio
  ownership all read rather than inferred.
- **M9: yes, effectively** — `RadioLibWrappers.cpp:14` keeps radio state in a
  file-static flag set from an ISR. One radio per firmware image, structurally.
- **M6 corrected during the reconciliation:** MeshCore supports exactly one of
  the T-Watch's five candidate radios, and CC1101 is compiled out. An earlier
  version of that answer conflated RadioLib *driving* a chip with the chip being
  able to do LoRa.

### T-015 · ADR-0004: capability sources and their runtime lifecycle — 2026-08-21
- Seven availability states, one per user remedy; `Origin` as an orthogonal
  axis argued from a nine-row call-site table; a centrally-owned transition
  table; two ages on every datum that crosses a link; capabilities separated
  from data feeds. **Accepted** — final §8 endorses the model by name.

### T-017 · ADR-0006: settings, and values bounded by law — 2026-08-21
- Frequency is `uint32` Hz, never float — measured: `868.731f` round-trips to
  868 731 018 Hz, and one ULP at that magnitude is 64 Hz. Three scopes, three
  distinct power ceilings, a network contract applied as one atomic preset,
  stage→confirm→auto-revert for remote writes, layered factory reset, and an
  `Unknown` region profile that closes the transmit path. **Accepted** — final
  §34–§38 restate it independently.

### T-023 · Reuse-ledger records — 2026-08-21
- Six full records with commit hashes, licence checks and lessons drawn from
  upstream issues and reverts rather than from happy-path source. The ledger no
  longer says "Records: Empty" above actual records, which is the state final §67
  names.

### T-019 · The node as a documented profile — 2026-08-21
- [NODE_PROFILE](docs/node/NODE_PROFILE.md): five established facts, ten open
  questions, each with what it blocks. The node stays out of
  [HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md) until a part number exists.

### T-003 · Host build and CI — 2026-08-21
- Plain-CMake host build plus a test target, green in GitHub Actions.
  `cmake -S . -B build && cmake --build build && ctest` passes.

### T-005 / T-031 · Toolchain installed and verified — 2026-08-21
- ESP-IDF `v5.5.5-496-gc197d718bcc`; `idf.py set-target esp32s3 && idf.py build`
  completes on a stock example. `ninja`, `SDL2`, `ccache` present.
- The first install attempt failed and the reason is worth keeping: `python3` on
  this host resolves into an unrelated virtualenv, and ESP-IDF's `install.sh`
  refuses to build a virtualenv from inside one. It succeeds with that path
  element removed.

### T-001 · Core coverage for the full peripheral inventory — 2026-08-21
- Every part on both boards has an owning core service, including the parts the
  vendor BSPs ignore and the ones no application uses.
- Established *why*: an unowned part still costs power, still raises interrupts,
  still contends for the bus, and still floats its pin. "Maybe useful later" was
  never the argument.
- **Amended 2026-08-21:** "owns" no longer means "initialises". Final §32 names
  that definition as too strong, and this board proves it — GPIO 6 may be driven
  by the radio as a TCXO supply, so configuring it to satisfy a checklist is how
  the oscillator gets shorted.

### T-002 · ADR-0001: capability model — 2026-08-21
- Delivered the first capability model: presence, typed descriptors for variant
  and degree, a separate availability axis. Four alternatives recorded with
  reasons, all four still rejected.
- **Its Decision has since been superseded twice in one day** — by
  [ADR-0004](docs/adr/0004-capability-sources.md) for the Attadipa node, then
  wholesale by [ADR-0007](docs/adr/0007-two-capability-layers.md). The task
  stays DONE: it produced a decision, a review found it wrong, and that is the
  process working rather than failing.

### T-000 · Repository, research gate, and board survey — 2026-08-21
- Repository created, MIT, public.
- Both boards surveyed from vendor documentation, vendor BSP source and
  published schematics — then the schematics were **read** rather than cited,
  which corrected two rows and produced two documented conflicts with the vendor
  documents.

### T-090 · The corrections the Waveshare verification pass turned up

- **Priority:** P2 — none of these blocks anything today, and every one of them
  is a wrong fact sitting in a document another agent will read as true.
- **Dependencies:** none. Each is a small correcting commit.
- **Goal:** close out the defects listed in
  [WAVESHARE_ARRIVAL.md](docs/research/WAVESHARE_ARRIVAL.md) §7 that are ours
  rather than the external advice's. **Five of the seven** are already done on
  the branch that filed this task — the peripheral table's missing columns, the
  reuse ledger's wrong upstream, D3's mis-stated connector, the false promise in
  VERIFIED_FACTS §1, and the D12 split propagated to all three of the places it
  had been left out of. **Two remain:**
  - [`docs/upstream/research-integration.md:180-181`](docs/upstream/research-integration.md)
    says "Both Attadipa boards are ESP32-S3**R8** modules with PSRAM" and rests a
    ~10 µA light-sleep floor on the workaround "must not be deselected on a
    module rather than a bare chip". [HARDWARE_MATRIX.md:301](docs/research/HARDWARE_MATRIX.md)
    records the Waveshare SoC as a **bare chip, not a module**, VERIFIED from the
    schematic. One of the two is wrong, the figure is carried forward into
    [HIL_PLANS.md:64-67](docs/testing/HIL_PLANS.md) as VENDOR-STATED, and the
    sleep-current plan depends on which.
  - The part-ownership table at
    [ARCHITECTURE.md:396-414](docs/architecture/ARCHITECTURE.md) has no flash or
    PSRAM row for the Waveshare where the T-Watch table has both. An omission,
    not a claim — but CLAUDE.md says every part on the board gets a seat.
- **Not in scope:** D13's rail assignments. That needs the board.

### T-091 · Two more addresses on the Waveshare I2C bus, and a board profile that knows it

- **Priority:** P2 — it is wrong today and it is cheap.
- **Dependencies:** T-090 is unrelated; this one waits on nothing.
- **Goal:** the ES8311 codec and the ES7210 microphone ADC are I2C control slaves
  on the main bus, which the vendor BSP demonstrates by handing all three parts
  one `i2c_master_bus` handle. The board profile and any future bus-collision
  check must carry six addresses, not four. Recorded in
  [VERIFIED_FACTS.md](docs/research/VERIFIED_FACTS.md) and
  [HARDWARE_MATRIX.md](docs/research/HARDWARE_MATRIX.md); nothing in `platform/`
  models an I2C bus yet, so this is a note against whoever writes that first.
- **Carry the trap with it:** SensorLib's `QMI8658_L_SLAVE_ADDRESS` is `0x6B`
  where `L` means the SA0 *pin level*, and Waveshare's `QMI8658_ADDRESS_HIGH` is
  also `0x6B` where `HIGH` means the *numeric value*. The two vendor demos look
  like they disagree and do not. Any Attadipa wrapper that re-exports either name
  hands the next reader the same trap.

### T-092 · Do not depend on Waveshare's `esp_lcd_sh8601` fork

- **Priority:** P2 — it decides part of T6 with evidence rather than preference.
- **Dependencies:** feeds open question T6.
- **Goal:** `waveshare/esp_lcd_sh8601` is a two-line fork of
  `espressif/esp_lcd_sh8601` — its own files carry Espressif's SPDX headers. One
  line is inert. The other, at `:280`, calls `tx_color(...)` bare where upstream
  wraps it in `ESP_RETURN_ON_ERROR`, inside `panel_sh8601_draw_bitmap`, which
  then returns `ESP_OK` unconditionally: **a failed frame transfer is reported as
  success.** Present in 1.0.2, which the published demo pins, and in 2.0.0.
  Espressif ships both an unforked `esp_lcd_sh8601` and a purpose-named
  `esp_lcd_co5300` — QSPI, accepting a custom init table — under the same
  Apache-2.0. Take the pin map and the init table; depend on upstream.
- **Evidence:** [WAVESHARE_ARRIVAL.md](docs/research/WAVESHARE_ARRIVAL.md) §3.3.

### T-093 · The LVGL draw-buffer ADR has no vendor existence proof to lean on

- **Priority:** P1 — it was about to be written on a false premise.
- **Dependencies:** the arithmetic is done; the numbers that matter need hardware
  (§6 rows 9 and 10).
- **Goal:** it is widely assumed that the vendor BSP proves PSRAM-backed LVGL
  works at 410 × 502. It does not. `bsp_display_start()` sets
  `.buff_spiram = true` and it is **dead code** —
  `bsp_display_start_with_config()` reads only `cfg->lvgl_port_cfg`, and the live
  allocation in `bsp_display_lcd_init()` is `410 × 100 px` ≈ 80 KiB with
  `.buff_dma = false` and `.buff_spiram` guarded by `CONFIG_BSP_DISPLAY_LVGL_PSRAM`,
  a symbol that appears **zero times** in the BSP's Kconfig. So the vendor ships
  one partial buffer in internal SRAM. If anything that points away from PSRAM.
- **The hardware constraint to carry in:** on the ESP32-S3
  `SOC_PSRAM_DMA_CAPABLE` is 0, so a draw buffer in PSRAM can never also be
  DMA-capable.
- **The arithmetic, which reproduces independently:** 410 × 502 = 205,820 px; one
  RGB565 frame is 411,640 B = **402.0 KiB**, 78.5 % of the 512 KB internal SRAM
  before ESP-IDF, the QSPI driver and BLE exist. Double-buffered internally is
  arithmetically impossible; double-buffered in 8 MB of PSRAM is 9.8 % of it.
  Capacity is not the constraint — internal SRAM, PSRAM bandwidth and cache
  coherency are, and only the board can measure the last two.
