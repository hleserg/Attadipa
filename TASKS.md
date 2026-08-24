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
- **Implementation status:** live. Seven workflows, an hourly watchdog, a
  half-hourly merge sweep applying the backstop's own path allowlist, and a
  daily backstop routine scoped to what a workflow cannot detect about itself.
  A second watchdog scan is written, tested and **not deployed**:
  [#75](https://github.com/hleserg/Attadipa/issues/75) — a run that completes
  `action_required` with **zero jobs** put no check on the pull request at all,
  so the orchestrator's *merge once CI is green* has no verdict to read and the
  agent's own "waiting on CI" is true forever. The `approvals` job comments once
  per head commit and deliberately does not re-run anything.

  **What blocks it is not the work**, and it is the same wall
  [#74](https://github.com/hleserg/Attadipa/issues/74) hit: GitHub refuses to
  let a GitHub App update anything under `.github/workflows/` without the
  `workflows` permission, which `claude[bot]` does not hold. The job, the
  `token:` line that is the actual fix, and the test's line in `ci.yml` all wait
  as `docs/automation/pending/75-approval-stall.patch`; landing it is three
  commands from a local session. Until then the stall is still silent **and
  still happening**.
- **One owner action is outstanding, and it is the fix rather than the guard.**
  The cause is the writer checkout leaving `actions/checkout`'s defaults, so the
  agent's own `git push` authenticates as `github-actions[bot]` and GitHub
  creates the resulting `pull_request` run in an approval-required state — a
  documented rule about the *token*, not a setting, and **no repository setting
  disables it**. The checkout now reads
  `${{ secrets.ATTADIPA_AGENT_TOKEN || github.token }}`, which changes nothing
  until that secret exists. **Create a fine-grained PAT scoped to this
  repository — *Contents: Read and write*, *Pull requests: Read and write*,
  ***Issues: Read and write***, and `Workflows` deliberately NOT granted — and
  set it as `ATTADIPA_AGENT_TOKEN`.** The issue permission is **not optional**:
  this secret is `github_token:` for `claude-code-action`, which posts the
  agent's report on the triggering issue and does its own labelling through
  `gh`. Without it the next issue-driven run 403s on the issue write, leaves no
  `agent:review`, and the watchdog re-queues a billable writer hourly with every
  run green. An earlier version of this bullet listed two permissions; the
  working scope in `CLAUDE_AUTOMATION.md` has always been three. Found in
  review.
  **Four costs are being accepted, and this bullet used to name one.** They are
  listed in full in
  [APPROVAL_STALLS.md](docs/automation/APPROVAL_STALLS.md); in short: attribution
  (agent commits carry the PAT owner's name rather than `claude[bot]`); a
  long-lived credential in `.git/config` in the one job the model holds `Bash`
  in (**T-146**); `claude-ci-repair.yml` becoming reachable, so a red run calls
  a second billable writer; and — the one that is not a trade-off but a
  condition — **the anti-recursion guard becomes unreachable** (**T-145**, P1). A
  fine-grained PAT belongs to a *user*, so the agent's own output carries
  `author_association: OWNER`, which `queue-scan.jq` accepts *before* the
  `claude`/`github-actions` login test is ever evaluated. Granting `Workflows`
  too would retire `docs/automation/pending/`, which is convenient and is exactly
  the widening that lets an agent rewrite the gate governing it.
  **So the recommendation is now B — a separate GitHub App — unless the PAT
  already exists**, because a second App keeps a distinct bot login and leaves
  the login test reachable. Taking A instead means landing T-145 first, not
  afterwards. This bullet said *"the cost being accepted is attribution"* and
  *"the alternative without the attribution cost is a separate GitHub App"* until
  the fifth review round of
  [#128](https://github.com/hleserg/Attadipa/pull/128) pointed out that both
  sentences are ones this branch had corrected in three other files while leaving
  the one `CLAUDE.md` sends an agent to first.
- **Tests:** `actionlint` over seven workflows with shellcheck integration —
  clean; `shellcheck -x` over both scripts — clean; intake gate, 16 hostile
  cases — 16/16; the approval-stall rule, 51 cases including both of #71's real
  runs with the values the API actually returned, the per-head marker rule, and
  the deployed `token:`/`approvals` lines themselves — 51/51, **not yet wired into
  CI** because that line is in `ci.yml` and rides the same blocked patch, and
  the watchdog job around it dry-run against the live repository (the jq, the
  pagination, the marker written and read back, the rendered comment) — which
  does not prove it running on a schedule under its own permissions, and that
  stays `NOT EXECUTED` until it is deployed; host build 10/10; simulator 12/12,
  both geometries. Production:
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


### T-114 · The debug channel needs a firmware end
- **Filed as [#117](https://github.com/hleserg/Attadipa/issues/117)**, whose
  transport-independent half is done: the protocol, the input layer, the bridge,
  the host tool, the diagnostic screen, the tests and the agent skill all exist
  and run against the simulator.
  - **Corrections to that half are this task, not a new one.**
    [#186](https://github.com/hleserg/Attadipa/issues/186) is the first:
    `gesture --file` did not spend the `duration` it was given — an `N`-point
    path waited `N - 2` intervals instead of `N - 1` and a two-point one waited
    none at all, so the shipped example spent 0.45 s of its declared 0.6 and a
    two-point gesture was a press and a release back to back. Fixed on absolute
    deadlines, pinned by three host groups on a fake clock and by a wall-clock
    bound in the end-to-end test; the semantics are now stated in
    [WATCH_CONTROL](docs/testing/WATCH_CONTROL.md#what-duration-measures).
  - **Still open, and deliberately not folded into that fix:** `swipe()` has
    the same shape of error one order smaller — `steps` intervals between its
    points, `steps - 1` sleeps, the first of them zero — so it runs `1/steps`
    short of the duration asked for and begins with a zero-length segment,
    where the gesture defect was categorical. Bounded and not urgent; it wants
    its own change and its own test rather than a widened diff. Two things go
    with it when it is picked up: `watch_control.py`'s `swipe`/`drag` print the
    *requested* duration (`"drag … in 1.2s"`) whatever was achieved, which is
    the misleading half; and `long_tap`, `button_click` and `swipe` still pass
    a duration straight to `time.sleep`, so a negative one surfaces as a
    `ValueError` from inside the standard library after the press has gone out.
    `gesture` refuses ahead of the press through `_duration_seconds`; the other
    three do not. Raised in review of
    [#187](https://github.com/hleserg/Attadipa/pull/187).
  - **A slow wire is silent, and that is a T-114 question.** `gesture` sends a
    point that is already overdue immediately rather than sleeping backwards,
    and says nothing about having done so. Over a Unix socket it never
    happens; over `SerialTransport` a five-point path at `duration: 0.05`
    gives a 12.5 ms budget per round trip and every point goes out late.
    Covered by a host test today (`a gesture absorbs its round trips rather
    than adding them`) but not *reported* to the operator — decide with the
    transport, where the achieved duration is worth printing beside the
    requested one.
- **Priority:** P2 today, **P1 the moment an ESP-IDF project exists.** Every UI
  task after that point is supposed to end with a real screenshot, and this is
  what makes one possible.
- **Dependencies:** an ESP-IDF firmware project. There is none — `README.md`
  says so — which is why this is a task rather than an omission.
- **Why it is separate:** the vertical
  `agent → host tool → protocol → input layer → UI → framebuffer → PNG` is
  complete except for the transport at the device end. `attadipa_debug` is a
  plain host-testable library precisely so the day the firmware exists it links
  the same code rather than growing a second implementation that drifts.
- **What is left, and it is small:**
  1. A transport that reads and writes framed bytes over **USB-Serial/JTAG**.
     `sim/debug_server.cpp` is the model, including the watermark: pump the
     screenshot only while the outgoing buffer has room, so the transport sets
     the pace and the interface keeps running. A transfer that blocked until the
     last of thousands of chunks had gone is a watchdog reset with extra steps.
  2. A `ScreenSource` over the real display. `lv_snapshot_take` again, for the
     same reason — one internally consistent frame. **Do not read pixels back
     out of the panel**: the AMOLED is behind QSPI and the CO5300's read path is
     not established (D7 has not settled even its init sequence).
  3. A frame buffer sized for the panel, behind the same config option, so a
     release build does not carry 617 kB it will never use. **The gate is about
     that 617 kB and not about who may look:** nothing here says the channel
     must be unreachable on a shipped watch, where it reads the screen and
     injects taps, and final §49 and the Definition of Done both point at it as
     a development instrument. Whether a released build carries it — and behind
     what — is a product decision nobody has taken, and it is not taken here by
     a config option chosen for RAM. Report **RGB565**
     with the byte order the driver actually produces — the wire format
     distinguishes `Rgb565Le` from `Rgb565Be` because guessing is how a
     screenshot comes back looking almost right.
  4. Report the driver's **orientation**. The field means *the rotation the host
     must apply, clockwise*; that direction is defined in
     `debug/protocol.h` rather than derived, and a driver that reports the
     opposite convention produces an upside-down image in half the cases.
  5. Route the real touch controller and the real buttons into
     `core::InputQueue` with `InputOrigin::Physical`. That is what makes remote
     and physical input coexist, and it is not extra work for this task — it is
     how the drivers should push events anyway.
  6. **Measure the capture pause, and fix the peak allocation.** Two numbers
     this milestone could not produce.
     - The pause is **3.6 ms** on a desktop for a 617 kB frame and `UNKNOWN` on
       a board, and the reason it cannot be scaled is in
       [WATCH_CONTROL](docs/testing/WATCH_CONTROL.md): the frame must live in
       PSRAM, which is octal at 80 MHz with a 10-cycle latency here (D12a), and
       a byte-at-a-time CRC over that is a different machine. Measure it. If it
       is over ~50 ms, take the CRC and/or the copy off the interface thread, or
       run the CRC block by block between frames.
     - `lv_snapshot_take` allocates **a second full frame** before the row copy,
       so the peak is ~1.24 MB rather than 617 kB. On a desktop that is
       invisible; on a board with 8 MB of PSRAM and a draw buffer already in it,
       it is a number to have decided about rather than discovered. Snapshot
       into the bridge's own buffer, or size the budget for two.
  7. **Three things the simulator gets away with and a device will not.** All
     were raised in review on
     [#121](https://github.com/hleserg/Attadipa/pull/121) and deliberately left
     here rather than fixed there, because each is an answer the firmware end
     has to give and none has a right answer on a host socket.
     - **Bound the injected coordinates at the device.** `_check_point` in
       `tools/watch/client.py:640` refuses a point outside the panel, and that
       is the *host* being polite. `Bridge::handle_input` validates the event
       type, the button index, the rate and the hold, and never looks at `x`
       and `y` — a client that does not use this tool, or a resync landing
       mid-body, injects a pointer wherever it likes. On the simulator LVGL
       clamps and nothing shows; on a device the coordinate reaches a driver.
       The device is the only end that knows the panel, so the check belongs
       there whatever the host does.
     - **Design the queue between the two tasks.** `core::InputQueue` is owned
       by one thread: `push` and `pop` read-modify-write a single plain
       `count_` from both ends, with no atomics, and `stats_` likewise. That is
       correct and cheap on the simulator, which is single-threaded, and it is
       what `bridge.h` implicitly promises to the device arrangement it
       describes — *a queue in front of it rather than a mutex inside it* —
       where the transport task and the interface task are different tasks. Two
       tasks on this queue lose an update: a lost increment strands an event
       while the `pushed == popped + flushed + size()` identity stops holding,
       and a lost decrement tears a five-field event across two taps. Either
       make it a real SPSC ring — separate head and tail, one writer each,
       release/acquire — or hand the crossing to an RTOS queue and leave this
       one behind the interface task. The header now says one thread owns it;
       this is where that stops being enough.
     - **Decide the poll cadence rather than inherit it.** `sim/main.cpp:289`
       runs the loop at 5 ms with a client attached and 50 ms without. Those
       two numbers were chosen so a 600 kB screenshot over a Unix socket does
       not take a minute, and they are a *desktop* answer: on ESP-IDF the
       interface task has a priority, a watchdog and a tick rate, and the
       transport is USB-Serial/JTAG rather than a socket that never blocks.
       Copying the numbers across is the failure this file keeps naming — a
       host measurement worn as a device fact. Pick the pacing from the
       watchdog and the transport's own back-pressure, and write down which.

- **Acceptance:** `tools/watch_control.py info` answers over a port **resolved
  from the unit's USB serial and not named on the command line** — `ttyACM0` is
  not an identity on this host and T-116 is the resolver — a
  screenshot of the real panel is written and **looked at**, the diagnostic
  screen shows the corners and colours where they belong, and a swipe leaves a
  trail rather than two dots. `tools/watch/e2e_test.py` is the model for what to
  check; it will need a variant that talks to a port instead of starting a
  simulator.
- **What must not be assumed:** that `SerialTransport` in
  `tools/watch/client.py` works. It is written and **has never spoken to a
  device**, because there has been no device to speak to. It is marked
  `NOT EXECUTED` in its own docstring and must stay so until it has run.
- **Hardware required:** yes, and **flashing needs the owner's authorisation**
  ([CLAUDE.md](CLAUDE.md)). The received Waveshare currently runs the vendor's
  own firmware and is byte-identical to the T-099 backup.

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


### T-143 · A band is not readable off the part, and nothing says so but a comment
- **Priority:** P2
- **Dependencies:** the T-Watch arriving (T-106 bring-up). The data-model half
  can be designed now.
- **Goal:** `radio_info_for()` publishes RadioLib's **driver** limits as a
  chip's coverage — `{150 MHz, 960 MHz}` for the SX1262 — and `RadioInfo` has
  nowhere to record that this particular unit's matching network and antenna
  were never looked at. So the moment somebody sets `RadioChip::Sx1262` from a
  marking alone, `covers()` answers yes for EU868, US915 **and** AS433 at once —
  three regional networks one unit cannot all be built for — and
  `MeshMessaging` goes Ready. The code is not lying; the observation is missing.
  A2's answer names 868 MHz, and only the chip half of it is readable off the
  part: band is set by the matching network, and is readable neither over SPI
  nor off the package, so the 868 rests on the same seller's listing this
  project refuses for the chip (ADR-0003).
- **Acceptance:** `RadioInfo` carries the band as an *observation* with its own
  provenance, distinct from the driver's tuning range, and `covers()` answers
  from the observation or says it cannot say. A capability derived from an
  unobserved band never reaches `Ready`. Host tests for: driver range present
  and band unobserved, band observed and narrower than the driver's, and the
  three-regions-at-once case above asserted to be impossible once observed.
- **Watch for:** this is the second half of the same lesson as
  `test_shipped_twatch_radio_is_unread` — a comment is not a check.
  `test_sx1262_bands_are_the_drivers_not_this_units` pins the trap today so the
  sentence cannot quietly stop being true, but pinning a trap is not closing it.
- **Hardware required:** for the observation itself, yes — the matching network
  is read off the board. The data model is not.

### T-140 · Fingerprint the citations into `HARDWARE_MATRIX.md`, which are all about thirteen lines out
- **Priority:** P2
- **Dependencies:** none — the mechanism exists; this is the sweep.
- **Goal:** `check_docs.py` check 7 can now hold a citation to the *text* it was
  written for, where the citation carries a fingerprint:
  `EXAMPLE.md:357 "Display FPC"` — the reserved placeholder path, because an
  illustration written with a real one is a live assertion about a document
  this task is not about. Three citations in `WAVESHARE_ARRIVAL.md` were out by
  thirteen lines or more and landed on a real, wrong row — one on Flash where it
  meant the display FPC, one on the tail of the GNSS-rail trap bullet where it
  meant PSRAM, and one into `VERIFIED_FACTS.md` that had drifted onto the AXP2101
  `PWRON` entry — and all three were repaired with fingerprints.
  **Forty-nine bare ones remain**, counted rather than remembered, and the
  number has now been wrong twice for the same reason. An early version said
  fifteen — a remembered subtotal of one file. The next said twenty-three, from
  a scan written to match *"the same shape the checker reads"* — and it did,
  faithfully, including the checker's two blind spots: a citation into a
  dot-directory matched at no position, and a bare basename resolved only beside
  the citing document or at the repository root. Both are fixed, and the count
  is now produced by importing `check_docs` and running its own resolution
  rules rather than by a scan that imitates them, so the two cannot drift apart
  again. The largest concentrations are still `WAVESHARE_ARRIVAL.md` — nine into
  `HARDWARE_MATRIX.md`, five into `OPEN_QUESTIONS.md` — with the rest spread over
  `TASKS.md`, `RECONCILIATION_2026-08-21.md`, `OWNER_DECISIONS.md` and six others,
  and twelve of them are into `.h`, `.cpp` and `.md` files that the old scan
  never looked at. Every one is a bare line number into a file that grows from
  the middle, so every one is a silent wrongness waiting. Repairing them without
  a fingerprint would only reset the clock.
- **Acceptance:** each of those citations resolves to the row or paragraph its
  sentence describes, carries a fingerprint that the checker reads, and
  `check_docs.py` is green. Where a citation cannot be given a fingerprint
  because the sentence does not name anything stable on the line, say so in the
  document rather than leaving a bare number — a section reference is better
  than a line number nobody can verify.
- **Watch for:** the numbering. This task is **T-140** rather than T-128
  deliberately: four open branches were each holding an unmerged `T-1xx` in the
  130s at the time it was filed, and taking the next free number on this branch
  is exactly how T-111 was claimed three times. Renumber at merge time if it
  still collides.
- **Hardware required:** no.

### T-126 · The merge sweep has still never merged anything
- **Priority:** P1
- **Dependencies:** the `--slurp`/`--jq` fix, which is what this checks.
- **Goal:** `pr-merge-sweep.yml` has run exactly once, by hand, and exited 1 in
  eleven seconds without reading a pull request. The fix is argued and unit
  guarded, but the workflow's own claim — that it merges what is finished — is
  still `NOT EXECUTED`. A workflow that has never completed its loop is not a
  working workflow, however good the diff looks.
- **Acceptance:** a dispatched run that reaches `sweep finished, N merged` and
  prints a per-candidate line for every open pull request, with the reason each
  was held. Then a run that actually merges one, on a pull request that was
  going to be merged anyway. Both run IDs recorded here. Until that second run
  exists, the orchestrator merges by hand and does not treat the sweep as cover.
- **Watch for:** the two conditions that can only fail on a real repository and
  not in a unit test — `mergeStateStatus` values GitHub returns that
  `merge-candidate.sh` does not enumerate, and a `gh pr merge` refused by branch
  protection, which the workflow deliberately turns into one loud failure rather
  than 48 quiet warnings a day.
- **Hardware required:** no.

### T-145 · The recursion bound must stop depending on who wrote the issue
- **Priority:** P1 — it becomes live the moment the owner sets
  `ATTADIPA_AGENT_TOKEN` to a fine-grained PAT, which T-108 asks them to do.
- **Dependencies:** none. It is a change to the intake rules and can land
  before or after the secret exists.
- **Why now:** the anti-recursion boundary is a **login-name** test.
  `intake-decision.sh` rejects `*"[bot]"`, `claude` and `github-actions`, and
  `queue-scan.jq` refuses the last two as producers, with the reason written
  beside it: *"our own output would start a billable writer: exactly the loop
  the allowlist was built to avoid."* A fine-grained PAT belongs to a **user**,
  so everything `claude-code-action` writes under it carries the owner's login
  and `author_association: OWNER` — and `queue-scan.jq` accepts on
  `author_association` **before** the login test is evaluated. The guard is not
  bypassed; it is unreachable, and no change to the login list can reach it,
  because the login really is the owner's. Found in review on
  [#128](https://github.com/hleserg/Attadipa/pull/128), where the document had
  claimed the opposite in as many words.
- **The two reachable consequences**, both today rather than hypothetically:
  - the agent files a blocker issue — which `CLAUDE.md` instructs, and the
    BLOCKED template the workflow shows it ends with *"How to resume: comment
    @claude on this issue."* — carrying `@claude` and the `attadipa-agent-task`
    marker. `queue-scan.jq` matches on the marker with no label needed and the
    watchdog dispatches a billable writer on our own output, hourly;
  - `pr-merge-sweep.yml` clears the unanswered-Codex hold on `select(.bot |
    not)`. Under the PAT the agent's comments are `user.type == "User"`, so the
    agent answers Codex on the owner's behalf and the one rule that can put a
    commit in `main` unattended merges over findings nobody replied to.
- **Goal:** a bound that does not ask *who wrote it*. Candidates, none decided
  here: a marker the agent stamps on its own output that the scanner refuses
  regardless of author; a per-issue dispatch counter in the watchdog, which
  bounds the loop rather than preventing it; or keeping a distinct bot identity
  by choosing Option B (a second GitHub App) over the PAT, which leaves the
  existing login test reachable and is the cheapest of the three if the owner
  has not already made the PAT.
- **Acceptance:** a test that fails against today's tree — an issue authored
  with `author_association: OWNER` carrying the agent's own marker must not be
  enqueued — plus whichever bound is chosen, and
  [APPROVAL_STALLS.md](docs/automation/APPROVAL_STALLS.md)'s *What this does not
  cover* updated to match what is then true.
- **What must not be assumed:** that setting the PAT is what creates the
  problem. It is what makes it reachable; the ordering bug in `queue-scan.jq`
  is already there.
- **Hardware required:** no.

### T-146 · The agent's push path leaves a long-lived credential in `.git/config`
- **Priority:** P2 — it becomes live with `ATTADIPA_AGENT_TOKEN`, and it is a
  bounded exposure rather than an open door, which is why it is not P1.
- **Dependencies:** none in code;
  [`pending/75-approval-stall.patch`](docs/automation/pending/README.md) is what
  makes it reachable.
- **Goal:** `actions/checkout` defaults `persist-credentials` to `true`, which
  writes the token into `.git/config` as an `http.extraheader` for the life of
  the workspace. The agent job is the one job where the model holds `Bash`, and
  it pushes with `git push` from that shell — so the persistence is what makes
  the push work and cannot simply be turned off. The built-in `GITHUB_TOKEN` it
  replaces expires with the job; a fine-grained PAT does not, so the same file
  goes from holding a credential that dies at the end of the run to holding one
  that does not. Found in review of
  [#128](https://github.com/hleserg/Attadipa/pull/128), where it was recorded as
  a price rather than fixed, because fixing it in that patch would have broken
  the push it exists to enable.
- **Acceptance:** a push path that does not persist a credential in the
  workspace — the candidates are `persist-credentials: false` plus an explicit
  remote URL set at push time from the secret, an App installation token minted
  per run (Option B in
  [`APPROVAL_STALLS.md`](docs/automation/APPROVAL_STALLS.md), which also removes
  the attribution cost), or a push step outside the job that holds `Bash`. It
  states which one and why, and it is **demonstrated to still push**: a change
  that closes the exposure and silently stops the agent from pushing is the
  worse outcome, because nothing goes red.
- **What must not be assumed:** that this is the generic *"a long-lived
  credential on a public repository"* already priced in `APPROVAL_STALLS.md`.
  That sentence is about the secret existing; this is about a specific file, in
  a specific job, readable by a specific tool.
- **Hardware required:** no.

### T-147 · A comment on a pull request hides it from the orphan sweep
- **Priority:** P2
- **Dependencies:** none; reachable today, and
  [`pending/75-approval-stall.patch`](docs/automation/pending/README.md) makes
  it likelier by adding a job that comments.
- **Goal:** `agent-queue-watchdog.yml`'s `stuck` job selects orphaned tasks with
  `select(.updated_at < $CUTOFF)` over `repos/$REPO/issues`, and that endpoint
  returns **pull requests too**. `updated_at` is bumped by a label, by any bot
  comment and by the watchdog's own note, so a pull request carrying
  `agent:working` that something comments on every tick never gets older than
  the two-hour cutoff and is never returned to the queue — lost in plain sight,
  which is the exact failure the `stuck` job was written to prevent.
  `merge-candidate.sh` already documents this hazard for itself and uses
  `committedDate` on the head instead: *"a label, a bot comment or this
  workflow's own note bumps it. What matters is when code last arrived."* Found
  in review of [#128](https://github.com/hleserg/Attadipa/pull/128).
- **Acceptance:** the orphan sweep ages a task by something a comment cannot
  move. It decides explicitly whether a pull-request-shaped task belongs in that
  sweep at all — and if it does, it ages it by head `committedDate` like
  `merge-candidate.sh`; if it does not, it says what returns such a task to the
  queue instead, because excluding it without a replacement path is the same
  loss with a different cause. A test that constructs a task whose `updated_at`
  is fresh and whose head commit is three hours old, and asserts it is swept.
- **Hardware required:** no.

### T-153 · A citation's fingerprint on the next line is silently unchecked
- **Priority:** P2
- **Dependencies:** none. Touches `tools/docs/check_docs.py` only, so it waits
  behind whatever else is in flight on that file rather than on any decision.
- **Goal:** `tools/docs/check_docs.py:498` "FINGERPRINT.match(line[match.end()"
  reads a citation's fingerprint out of the remainder of **the citation's own
  physical line**. A citation whose quoted snippet wraps onto the next line
  therefore has no fingerprint as far as the checker is concerned, and falls
  back to the blank-line test — which passes on any non-blank line, and that is
  precisely the rot the fingerprint was added to catch. It reads as protected
  and is not. Three such citations were found in `APPROVAL_STALLS.md` on
  2026-08-24: two in review of
  [#128](https://github.com/hleserg/Attadipa/pull/128), and the third only by
  re-counting that file's citations with the checker's own `CITATION` and
  `FINGERPRINT` patterns instead of by eye — which also caught its prose
  undercounting them, six against eight. Eye-counting is not a check, and a
  defect found three times in one file by hand is a missing check rather than
  three mistakes.
- **Acceptance:** `check_docs.py` reports a citation whose fingerprint sits on
  the following line, naming it a misplaced fingerprint rather than passing it.
  The signal is precise, not heuristic: fire only when what follows the citation
  on its own line is trivial — a stray backtick, bracket, comma or dash — **and**
  `FINGERPRINT` matches the start of the next line. A citation deliberately
  written without a fingerprint stays legal, because most are. Tests all three
  ways: a wrapped fingerprint is reported, a citation with no fingerprint at all
  is not, and one correctly fingerprinted on its own line is not. Reverting the
  fix turns the new cases red.

### T-157 · Prose cites a check by number and nothing reconciles the number
- **Priority:** P3 — small, and it has already produced two wrong instructions.
- **Dependencies:** none. `tools/docs/` only.
- **Goal:** `check_docs.py`'s docstring enumerates the checks 1–8 and the
  `CHECKS` tuple at the foot of the same file orders them, and until the third
  review round of [#152](https://github.com/hleserg/Attadipa/pull/152) the two
  disagreed: items 4 and 5 were swapped, from before the tuple existed. Nothing
  reconciles a docstring with a data structure, so the drift was invisible until
  prose started citing the numbers — at which point `TASKS.md` said *Check 4*
  for OD numbers and `STATUS.md` said *Check 5*, one of them wrong and neither
  checkable. Both are now the tuple's order, which is the order the tool prints
  and therefore the only one a reader can verify. That fixes the instance.
  Nothing stops it recurring, and the same round's headline finding was a stale
  count in the same docstring for the same reason.
- **Acceptance:** the suite reads the docstring's enumeration and asserts it
  matches `CHECKS` — count and order both — so reordering the tuple without
  touching the prose turns a case red, and vice versa. Cheap: the enumeration is
  `^(\d+)\. ` in `check_docs.__doc__`. Optionally also reject a `Check N` in
  `STATUS.md` or `TASKS.md` that names a check the tuple does not have at that
  position, which is the half that produced the two wrong instructions; if that
  is judged too clever, say so in the docstring rather than leaving the gap
  unstated.
- **And the count guard is narrower than it reads**, found in the fourth review
  round of [#152](https://github.com/hleserg/Attadipa/pull/152). It requires the
  *paragraph* to name the checker (`quoted_counts_that_disagree`), so prose that
  **describes** `check_docs.py` without naming the file is uncovered — which is
  where two stale claims were living, both in `ci.yml`: a step name listing
  seven checks and omitting open-question IDs, so a check-8 failure surfaced
  under a label that does not mention it, and a comment 24 lines above the one
  that *was* kept in step, saying *"Four failures"* and describing four. Both
  are corrected; neither was catchable. Widening the guard from "names the
  filename" to "names the checker" is part of this task, or the docstring says
  it does not.
- **What must not be assumed:** that this is the same guard as the count. The
  count guard (`CLAIM_FILES` in `test_check_docs.py`) reads *how many* checks
  are claimed; this is about *which check is which*, and the count was right in
  both files that got the number wrong.
- **Hardware required:** no.


### T-152 · A present provider that is permanently uncomparable still releases the hold
- **Priority:** P2 — narrow, and it is the residual of a fix rather than a new
  defect.
- **Dependencies:** `core/src/trust.cpp` (**done**); `provider_detached()`
  (**done**).
- **Goal:** `compare_provider()` calls `stop_awaiting(ProviderDisagreement)`
  when the second source cannot answer — no position, an out-of-range one, or a
  measurement older than `provider_comparison_window`. The first two mean the
  provider really has stopped being one. The third does not: a node relaying
  fixes at 1 Hz whose measurement times are consistently 6 s old is **present,
  is disagreeing, and stops being awaited anyway**, so the device climbs back to
  `Trusted` in about twenty seconds with the node still saying it is 550 m out.
  `14-a-relayed-fix-arrives-old.trace` records relayed ages of 2 s and 40 s over
  *"a link that can queue, retry and reconnect"*, so a persistently late relay is
  ordinary rather than exotic. Found in the second review round of
  [#153](https://github.com/hleserg/Attadipa/pull/153), which named it and did
  not block on it.
- **Acceptance:** it decides one of — a `TrustReason` meaning *a second source
  is present and cannot be compared*, which is a live reason that holds the
  state down and is retracted the moment one comparable frame arrives; or
  `stop_awaiting()` on the stale path is removed entirely and the only exits
  become an uncomparable-by-content frame and `provider_detached()`, with the
  owner obliged to call it; or the release is kept and the bound is stated where
  a reader meets it rather than only in this task. Whichever it picks,
  `tests/test_trust.cpp`'s
  `test_a_disagreement_stops_being_awaited_when_it_can_no_longer_be_compared`
  is rewritten to assert the decision — it currently pins today's behaviour with
  a deliberately disagreeing frame, so it will go red on purpose. Its two
  siblings are `test_a_node_under_cover_is_not_a_node_that_has_gone`, which
  pins that the grace is honoured, and
  `test_a_node_uncomparable_past_the_grace_stops_being_awaited`, which pins that
  it eventually expires; the first must stay green under any decision here, and
  the second is the one a new enumerator replaces.
- **What the fourth review round already settled, so this task starts from it.**
  The lift no longer keys on one uncomparable frame: it needs the other side to
  have been unable to answer for `provider_departure_grace`, and
  `provider_detached()` latches so that a detach is not silently timed against
  `evidence_ttl`. That closes the reachable half of the defect — a node under
  canopy, relaying fix-less frames at 1 Hz, was being read as a node that had
  gone, lifting the allegation about five seconds later and letting `remember()`
  commit the disputed coordinate as the fallback. What remains for this task is
  the *permanently* uncomparable present node, where the grace does eventually
  expire and the release is a bound rather than a bug.
- **And the grace is `ESTIMATED`, which is part of this task.** 120 s is chosen
  to sit above an ordinary urban dropout and below a boot; nobody has measured
  how long a node's receiver stays fix-less under cover, and the second source's
  duty cycle is not ours to know. Being generous costs a pinned `Degraded` for a
  node that vanishes without telling us; being stingy costs a false all-clear.
  It is a number to measure, not to argue about.
- **What must not be assumed:** that a new enumerator is free. Every
  `TrustReason` costs a bit in a mask that `DiagnosticsSnapshot` carries and a
  weight in `policy()`, and a reason nothing can retract is the pin this whole
  area exists to remove — the retraction has to be designed with it.
- **Hardware required:** no.


### T-154 · A node's own retained coordinate is still a comparable side
- **Priority:** P2 — the mirror of a defect already fixed on the local half, and
  reachable by the same ordinary event: a receiver losing its fix.
- **Dependencies:** `core/src/trust.cpp` — the local half is
  [#178](https://github.com/hleserg/Attadipa/issues/178) (**done**), and
  ADR-0011 §5.2 is the rule it established.
- **Goal:** `compare_provider()` decides whether the second source can answer
  from `other.position.has_value()`, `in_range()` and the measurement age.
  **None of those separates a coordinate a node's receiver solved for from the
  one it kept in the field after losing its fix** — which is exactly the
  distinction #178 drew for this device's own receiver, and it was drawn on one
  side only. A node under canopy relaying at 1 Hz therefore keeps disagreeing
  with the last place it knew about; or, worse, keeps *agreeing* with it, and an
  agreement reaches `clear()`, which is the only retraction this design has.
- **Why it is not simply "do the same thing again."** The local side has a
  `PositionValidity` because `observe()` is handed one. `compare_provider()` is
  handed a bare `GnssObservation`, and validity is a verdict `classify()`
  reaches with a `ValidityPolicy` — which the evaluator does not hold, and
  arguably should not, because how quickly a *node's* fix goes stale is the
  node link's question and not this receiver's.
- **Acceptance:** it decides one of — take the validity as a parameter, as
  `observe()` does, and leave the classifying to whoever owns the link; or give
  the evaluator a `ValidityPolicy` and classify inside, which is a new piece of
  policy and needs a reason; or read `other.fix_type`, which is already in the
  frame and answers a narrower question honestly (`NoFix` and `TimeOnly` cannot
  carry a solved position, `Unknown` means the receiver has not said). Whichever
  it picks, a retained coordinate from a node must not become a fresh side of
  the comparison.
- **What must not be assumed:** that refusing the frame is the same as calling
  the node gone. It is not, and
  `test_a_node_under_cover_is_not_a_node_that_has_gone` must stay green under
  any answer here — `provider_departure_grace` still bounds how long
  uncomparable may last before it means departed, and that is T-152, which this
  does not close. Nor that `fix_type` is free of consequences: a caller that
  leaves it at its default `Unknown` would start being refused, so whatever is
  chosen has to be stated where a caller of `compare_provider()` will meet it.
- **Hardware required:** no.


## NEXT

### T-128 · The generated-asset reproducibility job is written and cannot be pushed
- **Priority:** P2
- **Dependencies:** T-149 (**done**) — the script it runs is committed.
- **Goal:** add the `generated-assets` job to `.github/workflows/ci.yml`. It
  regenerates both committed asset trees from two different absolute paths with
  the pinned `lv_font_conv` and compares all sixteen files against what is
  committed — the one thing a stamp cannot do, because a stamp written beside a
  wrong file records the wrong file faithfully. The block is in
  [`tools/integrity/README.md`](tools/integrity/README.md), written against
  `actionlint 1.7.7` and passing it, together with the two one-line edits to the
  `evidence` job that go with it.
- **Why it is not already there, and it is not a decision:** the agent that
  wrote it authenticates as a GitHub App whose installation may not write
  `.github/workflows`, so the push was refused server-side — *"refusing to allow
  a GitHub App to create or update workflow `.github/workflows/ci.yml` without
  `workflows` permission"*. Working around that would mean smuggling a workflow
  change past a permission boundary somebody set on purpose.
- **Acceptance:** the job runs on a pull request and reaches `reproducible: 16
  generated file(s) are identical across two checkout paths and identical to
  what is committed`, and `evidence` lists it. Run ID recorded here.
- **Two ways to close it, and the owner picks:** paste the block in an
  orchestrator session whose token may write workflows, or grant the App
  installation `workflows: write` and hand this back to the queue. The first is
  one commit and changes no permissions; the second unblocks every future
  workflow fix an agent finds, and widens what an agent may push. **Recommended:
  the first**, because nothing else in the current backlog needs the second and
  a permission granted to close one task is a permission nobody revisits.
- **Watch for:** the job installs `pypng` and `lz4` from pip inside a venv —
  they are `LVGLImage.py`'s module-scope imports and are needed even with
  compression off. Pillow comes from apt, as it does in the other jobs.
- **Hardware required:** no. **Owner required:** yes, for the permission or the
  paste.

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
- **Does NOT carry D21, and an earlier version of this bullet said it did.**
  A mascot in `RGB565A8` is the first asset in this repository whose bytes have
  an order — true — but that order is not a fact about the panel. It is fixed by
  LVGL's colour-format contract and must match the framebuffer the software
  renderer writes into; the wire order is absorbed once, at flush, by the
  display port's `swap_bytes` flag. So **this task emits the asset in LVGL's
  format and reads nothing off D21**. Following the old instruction was not even
  possible for `RGB565A8`: the vendored converter packs it `uint16_t(color)` in
  host order and has no swapped variant to select — `--cf` offers
  `RGB565_SWAPPED` and no `RGB565A8_SWAPPED`. And for plain `RGB565` it would
  have bought nothing: a pre-swapped asset renders **correctly**, because LVGL
  un-swaps a swapped source while blending it into a native framebuffer
  (`lvgl@85aa60d1 src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c:935`), so all
  it costs is a conversion per blend that a native-order asset does not pay.
  Turning the **port's** swap off breaks things instead — every glyph, arc and
  `A8` icon LVGL renders into the same framebuffer, **and the asset with them**,
  because LVGL has already un-swapped it into native order by the time it is in
  the framebuffer. There is no configuration that leaves the asset right and
  only the glyphs wrong. D21 governs one board-level knob, in
  `boards/`/`platform/`, and the first line of display bring-up. Found in
  review; the *"wrong colours in both directions"* half of an earlier version of
  this bullet was over-stated and is withdrawn.
  **All of that holds while the LVGL destination is native-order, which is
  T-093's to decide and not a constant.** `RESOURCE_BUDGET.md`'s Avoidability
  row keeps the swapped-destination strategy live — LVGL has a whole
  `lv_draw_sw_blend_to_rgb565_swapped` target — and on that branch the guidance
  inverts and the pre-swapped asset is the free one. So the rule this task
  follows is the general one: **an asset's byte order follows the framebuffer
  the renderer writes into**, whichever that turns out to be, and never D21
  directly. Stated in the fourth review round of
  [#152](https://github.com/hleserg/Attadipa/pull/152), which found the
  absolutes bolted onto the correct rule going stale the moment T-093 decides.
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
  report taken on trust. Six from the same audit are already fixed —
  `d2bf02c` (the CRC did not cover the last byte), `f46578c` (three in the trust
  evaluator), `7e4c4f9` (the replay rig could not produce Stale), issue #26
  (the movement/altitude baseline, below) and issue #164 (the snapshot that
  claimed to be trusted, below) — and the rule from the research
  prompt applies: **do not stop after the first fix.** Issue #151 (recovery
  completing on silence alone, below) has since been closed the same way.
  **And one finding is about text rather than code.** `trust.h` and
  `diagnostics.h` both justify keeping a per-reason mask by what a diagnostic
  screen will show — *"can name it rather than showing a device stuck for no
  visible reason"* — and there is no `l10n/strings.toml` entry for any
  `TrustReason` or `TrustState`. `to_string(TrustReason)` returns English
  identifiers, which is right for a diagnostics dump and is not a sentence a
  user reads, so the promise is currently kept by something that cannot keep it.
  Either the strings exist, or the promise says *a support bundle* instead of *a
  screen*. Pre-existing; made twice more by
  [#153](https://github.com/hleserg/Attadipa/pull/153), which is why it is
  written down here rather than left to the next reader of a header.

- **A state that cannot say "nobody has checked" — fixed, issue #166.**
  `GnssCapabilities` was four plain `bool`s defaulting to `false`, so "this
  receiver has no backup domain" and "nobody has read the datasheet yet" were
  the same value. T-051 and T-052 exist precisely because those answers are not
  yet known, and the type could not hold the state the project is actually in.
  The four fields are now `SupportState` — `Unknown` · `Unsupported` ·
  `Supported`, defaulting to `Unknown` — which is what
  [ADR-0011](docs/adr/0011-gnss-integrity.md) §3 had already decided for the
  receiver capability descriptor and what `ReceiverIndication` and OD-5 apply
  elsewhere. `is_supported()` gates the three planner decisions, so `Unknown`
  stays fail-safe: no `Backup`, no `PowerSave`, no warm start promised. The
  provenance survives the decision rather than being flattened into it —
  `is_established()` and `fully_established()` are how "T-051 is finished"
  becomes a check a machine makes instead of a field somebody remembers, and
  `to_string(SupportState)` keeps `Unknown` sayable on a diagnostics screen. A
  scoped enum also means `GnssCapabilities{false, false, false, false}` no
  longer compiles, and `tests/CMakeLists.txt` pins that with a compile-fail test
  beside the two layer boundaries, so the collision cannot return quietly.

- **A default-constructed snapshot claimed to be trusted — fixed, issue #164.**
  `GnssStatus::trust` (`core/include/attadipa/core/diagnostics.h`) defaulted to
  `TrustState::Trusted`, so a snapshot nobody filled in reported the most
  reassuring answer available — at boot, in a panic handler, and on a board that
  has no receiver at all. `validity` on the line above defaults to `NoFix`,
  which was the right instinct. The field is now
  `std::optional<TrustState>`, empty by default, which is the idiom the rest of
  that header already uses for facts nobody has produced; the two candidate
  answers in the original finding were both weighed and **`Untrusted` was
  rejected**, because it asserts that a verdict was reached and was bad, which
  is a different claim from "no verdict" and one that anything counting
  integrity alarms would believe. Not a fourth `TrustState` either: that enum is
  ordered and compared against thresholds throughout `trust.cpp`.
  `trust_reasons` moves with the verdict through `record_trust()` /
  `forget_trust()`, so a mask cannot outlive the evaluation that produced it,
  and `to_string(std::optional<TrustState>)` gives a log or a support bundle the
  word `NotEvaluated` rather than a blank or an enum zero — a diagnostic
  identifier, not a screen string (ADR-0010 §4). A stored verdict is read
  through `trust_or(stored, when_not_evaluated)`, because the optional's
  comparisons against a bare `TrustState` compile and `!= Untrusted` is *true*
  while empty, which is a navigation guard failing open. Eight
  regression tests in `tests/test_diagnostics.cpp`, including the round trip
  through the panic-handler `memcpy` for all three real verdicts; restoring
  either candidate default turns them red. Recorded as an amendment to
  [ADR-0011](docs/adr/0011-gnss-integrity.md) §5. The snapshot did not grow:
  the extra byte fits existing padding, so `GnssStatus` is 40 bytes and
  `DiagnosticsSnapshot` 384 before and after.

- **Still open, raised by the review of that fix: the verdict and its reason
  mask are paired by discipline, not indivisibly.** `record_trust()` and
  `forget_trust()` (`core/include/attadipa/core/diagnostics.h`) are two stores
  each, and the consumer this snapshot exists for is a panic handler — an
  exception context that can land between any two instructions of the task
  filling it in. The result is the one pairing the header says cannot happen: an
  empty verdict beside a live reason mask, in the artefact somebody reads after
  a crash. Nothing single-threaded can reach it and no store order avoids both
  interleavings. Making it indivisible means packing the verdict into spare bits
  of `trust_reasons` — there are 15 reasons and the mask is 32 bits, so two bits
  are free — which changes the shape of the field and should be decided when
  something actually writes a snapshot from a panic path, not before. Filed so
  that the assumption is written down rather than inherited.

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

- **A degree of longitude near the pole measured a thousand times too far —
  fixed, issue #28.** Not from this audit: a continuous review of
  `259d4c8..10ab4fd` found it, and it is recorded here because this is where
  defects in already-shipped code are tracked rather than because the two
  reviews are the same. `lon_e7_to_mm` (`core/src/geo.cpp`) indexed
  `kCosTable1024` with the *truncated* degree of the mean latitude, so every
  latitude in [89.0°, 90.0°) was scaled by `cos 89°`. A degree of longitude at
  89.9°N is about 194 m; the function returned 1 956 796 mm, ten times over,
  and a thousand times over by 89.999° — the error grows without bound because
  inside that last degree the true scale falls to zero while a step function
  holds its last value. Downstream, `TrustEvaluator::observe` divides that
  distance by an interval to get implied speed and compares it against a still
  wrist, so a stationary device at a high latitude would have produced
  kilometre-scale movement and a `PositionJump`, for being at a high latitude.
  Fixed by interpolating between the two bracketing table entries at the full
  1e-7-degree resolution, in integers, with the products bounded by hand and
  the table, the units and the `distance_mm()` contract unchanged. The error
  envelope is now stated in `geo.h` and **measured on every test run** against
  an independent haversine reference rather than asserted in a comment: within
  0.9% from the equator to 89.999°, and under 20 mm inside the last 111 m where
  a whole degree of longitude is under two metres. Regression matrix in
  `tests/test_position.cpp`; **10 017 assertions** fail against the pre-fix
  code, though **twenty-one new check sites are green against it** — the whole
  of `test_the_grid_boundaries_are_answers`, which never reaches the cosine
  index, and the three invariants of
  `test_the_longitude_scale_is_monotonic_symmetric_and_bounded`, which a step
  function satisfies too. Worth keeping, not evidence of this defect; an earlier
  version of this line said *every* new check was red. The pre-existing distance
  tests all passed against the pre-fix code, which is why the suite could not
  see this.

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

- **`Attach` while `Faulted` reported the wrong refusal — fixed, issue #158.**
  It returned `Redundant` where `Ignored` is the truth: nothing about a faulted
  link makes a new attach redundant, and the two words tell an operator
  different things. The cause was one guard, `phase_ != Absent -> Redundant`,
  answering for five phases at once, and the diagnostic cost was the larger
  half — `Redundant` is not counted, so a controller retrying an attach against
  broken hardware left nothing in `ignored_events()` to find. `Attach` is now
  classified per phase: `Absent` applies it, `Attached`/`Connecting`/`Ready`
  answer `Redundant` because the peripheral genuinely is there, and `Faulted`
  and `Suspended` answer `Ignored` and are counted. **`Suspended` is the
  contract that had never been decided**, and it is decided here: a quiesced
  link carries nothing, so an attach is refused rather than satisfied and the
  way back stays `Resume` — otherwise a lifecycle owner that had not noticed
  the suspend could route around it silently. A phase added to the enum later
  falls to `Ignored` rather than `Redundant` in `link_state.cpp`, which is the
  safe half of the two and is deliberately not compile-time guarded; the guard
  is in **the test**, where a `constexpr` coverage check over
  `kTransportPhaseCount` fails to build if the phase table does not name every
  phase. So a new phase compiles and behaves safely, and the suite refuses to
  build until somebody has decided what it *should* do.
  Mutation-verified: restoring the old guard turns 13 checks red across the
  three new tests, and leaves the `Attached`/`Connecting`/`Ready` rows green,
  which is the evidence that only the two intended phases moved.

- **`Detach` hardcodes `PeerClosed`.** A detach the *device* initiated is
  recorded as one the peer initiated. That is the field-report evidence for the
  most common question about a node link — who let go first.

- **Recovery could complete with no observation at all — fixed, issue #151.**
  The clean-hold window could elapse while nothing whatever had been reported,
  so silence promoted the state. With the default policy the whole path was
  deterministic: `report(ReceiverSpoofing, t=0)` reached `Untrusted`, the TTL
  took the alarm out of the score at 15 s, `evaluate()` read the resulting zero
  as a detector's all-clear and started the clean hold on it, and twenty-five
  seconds after the alarm the device announced `Trusted` again — no observation,
  no `clear()`, no evidence of any kind in between. OD-5's rule is that silence
  is not an all-clear, and this was the one place the code still treated it as
  one. `TrustEngine` now remembers, per reason, whether an allegation left
  `live_` by `clear()` or by the TTL, and refuses to start or advance the
  recovery hold while any of them stands unretracted
  (`TrustEngine::unconfirmed_reasons()`); when a retraction does arrive the hold
  is measured from it rather than from the silence in front of it. Descent,
  hysteresis and one-step-per-hold are unchanged. Six regression tests in
  `tests/test_trust.cpp` and replay trace
  `16-silence-does-not-restore-trust.trace`; removing the gate alone turns
  sixteen checks and four trace expectations red. The rule is now written down
  as [ADR-0011](docs/adr/0011-gnss-integrity.md) §5.1, including what it costs:
  a device that never hears another positive word does not climb back on its
  own, and the ways out are a detector saying so, `reset()`, or
  `stop_awaiting()` **when the provider goes away** — three, not two, and not
  *never a timer*: past `stop_awaiting()` the recovery hold runs and the state
  climbs on the clock with nothing retracted, which is legitimate only because
  the allegation was about a pair and one of the pair is gone. The scope on
  `reset()` is load-bearing:
  it asserts `Trusted` immediately and skips both holds, so it answers *a
  different provider is here now*, and the pin most likely to be met comes from
  the device's own receiver, which never detaches. It is also per boot, nothing
  in `core/` persisting trust state. **Three further findings on the second
  review pass**, one blocking: `ProviderDisagreement`'s only retraction sat
  behind `compare_provider()`'s freshness gate, so a duty-cycled receiver or a
  relayed fix measured outside the window pinned the device for the rest of the
  boot with no live reason and no exit — fixed with `stop_awaiting()`, which
  clears the *awaiting* bit without touching a live allegation, mutation-checked
  in both directions; `DiagnosticsSnapshot` could not carry the mask that
  decides the verdict, so a stuck device could not say why on the one screen
  meant to explain it, and `GnssStatus` gained `trust_unconfirmed`; and the
  claim that only three reasons can reach the mask is **false** whenever
  `observe()` does not run inside `evidence_ttl`, because `refresh()` retracts
  only `FixLost` and `StalePosition` — with the shipped defaults that is a
  fifteen-second window of `Degraded` with `score() == 0`, self-healing on the
  next observation, so a corrected sentence rather than a code change.

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
- **Implementation status:** in progress — the items marked *fixed* above are
  closed, each with a mutation-verified test, and the rest are open with this
  task. Closed so far: the `Attach`-while-`Faulted` refusal (issue #158), and
  silence after a GNSS alarm restoring `Trusted` on its own (issue #151). The
  remaining bullet in the same file as the first — zero meaning two things in
  the decoder — was deliberately left alone, so that one finding stays one
  change.
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
- **Four of those entries already have a runtime home.** Since issue #166,
  `GnssCapabilities` holds `backup_domain`, `power_save_mode`, `assistance` and
  `orbit_prediction` as `SupportState`, all four `Unknown` today, and
  `fully_established()` is the mechanical check that this task actually closed
  them — a profile with a gap fails it. Backup and hot start are the two the
  power model reads, so answering them changes behaviour rather than only a
  document.
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
- **Same four runtime entries as T-051**, and the same `fully_established()`
  check. This variant is where an unsourced `Supported` would be most tempting,
  which is exactly why `Unknown` is a value the type can hold.
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
  reboot path (#2627); **and `DiagnosticsSnapshot` carries a version, a magic
  and its own size** before anything writes it anywhere. `tests/test_diagnostics.cpp`
  pins a memcpy-into-RTC contract and the struct has no stamp of any kind, so a
  snapshot written before an OTA and read after it is garbage with nothing to
  detect it by. Latent today because nothing persists one; the layout has now
  changed twice
  ([#153](https://github.com/hleserg/Attadipa/pull/153),
  [#163](https://github.com/hleserg/Attadipa/pull/163)) without a reader
  noticing, which is exactly the condition for the defect. Found in review of
  #153.
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
- **Implementation status:** not started as a task — the six adversarial agents
  allocated to this terminated on an account spend limit and returned nothing.
  Two of its scenarios have since been resolved from the outside, by
  [#174](https://github.com/hleserg/Attadipa/issues/174): *the link drops
  mid-navigation* and *the node's firmware is too old to speak our version* both
  produced a wrong answer, and it was the kind this task is looking for rather
  than an omission — `provider()` reported a node that had merely gone out of
  range as the local device. Fixed, and both are now host tests in
  `tests/test_capability_registry.cpp`. The remaining scenarios are untouched,
  and the sharpest one below is still open.
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
- **Dependencies:** T-086, and T-089 (the wrist-raise gesture)
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

### T-089 · Display wake sources: raise gesture, button, touch — accelerometer only

- **Priority:** P1 — [OD-20](docs/research/OWNER_DECISIONS.md#od-20--a10-the-display-wakes-on-raise-button-or-touch-the-raise-gesture-reads-the-accelerometer-only)
  makes display-off-by-default the product answer to A10, and T-087 (living
  watch faces) already lists this as a dependency it cannot start without.
- **Dependencies:** T-045 (`PowerState`), T-061 (step counting — same sensor,
  same `MotionSensing` capability), [OPEN_QUESTIONS H16](docs/research/OPEN_QUESTIONS.md)
  for INT1's electrical polarity, H8 for whether the IMU rail survives SoC
  sleep on either board.
- **Goal:** implement the three display-wake sources OD-20 names — wrist raise,
  button, touch — as one capability an application never has to distinguish by
  board.
- **Wrist raise is accelerometer-only, on both boards, and that is fixed by the
  hardware rather than by preference.** The T-Watch's BMA423 carries no
  gyroscope; the Waveshare's QMI8658 does. A gesture built on gyro data would
  therefore be a feature that silently exists on one board and not the other —
  the thing `core/` and `apps/` are never allowed to know. Built on the
  accelerometer's gravity vector, both boards answer yes. Do not read
  `GYROSCOPE` availability anywhere in this path.
- **The interrupt routing is now known, and changes the shape of the
  implementation.** [WAVESHARE_ARRIVAL §3.2a](docs/research/WAVESHARE_ARRIVAL.md)
  traces the Waveshare's QMI8658 INT1 to ESP32-S3 GPIO 21 — inside the SoC's
  RTC-IO range, so `esp_sleep_enable_ext0/1_wakeup()` can arm it — and the
  T-Watch's BMA423 INT1 is already `VERIFIED` on GPIO 14, the same range on the
  same SoC family (`HARDWARE_MATRIX.md:100`). Both IMUs give exactly one usable
  interrupt line (the second is bonded out on the T-Watch, routed only to a
  test point on the Waveshare), and on the T-Watch that one line is already
  shared six ways by LilyGo's own board support (step counter, any-motion,
  no-motion, activity, tilt, wake-up) via a status register read — design for
  a shared line on both boards rather than a private one.
- **What must not be assumed:** that INT1's polarity or push-pull/open-drain
  configuration is known — it is not (H16); that the IMU rail survives SoC deep
  sleep — H8 is `CONFLICTING`, not answered; that button GPIOs are known — the
  extraction never resolved them (D5, `HARDWARE_MATRIX.md:353`), so the button
  wake source may need its own schematic pass first.
- **Of OD-20's three wake sources, only one has a traced deep-sleep path on
  the Waveshare, and it is the raise gesture.** Checked against the matrix
  rather than assumed:
  - **Touch cannot wake it from deep sleep.** `HARDWARE_MATRIX.md:344` puts
    the FT3168 interrupt on **GPIO 38**, and the RTC-IO range this task relies
    on for INT1 is `0`—`21`. 38 is outside it.
    `esp_sleep_enable_gpio_wakeup()` reaches arbitrary GPIOs in **light**
    sleep only — so "touch wakes it" and "it deep-sleeps" cannot both hold on
    this board. The T-Watch genuinely differs: its touch interrupt is GPIO 16
    (`:97`), inside the range. Light sleep is not free either:
    [ESP32S3_ERRATA_V02.md:52](docs/research/ESP32S3_ERRATA_V02.md) records
    RTC-126 as light-sleep-only with its cost **UNKNOWN, NOT MEASURED**.
  - **The button has no traced wake path at all.** `HARDWARE_MATRIX.md:353`
    leaves the Waveshare's button GPIOs unresolved (D5), and
    [WAVESHARE_ARRIVAL](docs/research/WAVESHARE_ARRIVAL.md) item 14 records
    that `AXP_IRQ` appears in **no row** of the schematic GPIO table — the
    same table §3.2a used to settle INT1. On the T-Watch the power button
    reaches the SoC only as a PMU interrupt (`:113`), so "there is a button"
    and "the button can wake it" are again different sentences.
  - **Why that reaches past battery life.** Display-off-by-default makes the
    wake path the first leg of the SOS path, which final §88 requires to have
    *"no dangerous delay"* and §49 puts in Child Mode. If the only proven
    deep-sleep wake is a raise gesture, the fallback for someone who cannot
    perform one on demand is unspecified. **This task must state which state
    the device is in when SOS must be reachable, or say that it is deferred
    and to what.**
- **Two wake sources with opposite trigger levels cannot share one `ext1`
  mask.** `ext0` takes one pin; `ext1` takes one mask and **one level mode for
  all of it**. This repository already holds the fact and had not carried it
  here: [meshcore-1.17-review.md:393](docs/upstream/meshcore-1.17-review.md)
  records from upstream #1347, on an ESP32-S3, that *"a button and DIO1 cannot
  share an ext1 mask because they need opposite trigger levels"*, and marks it
  `ADAPT` — *"the ext1-polarity fact is a hardware fact worth carrying"*. A
  button to ground is active-low by construction; INT1's active level is
  `UNKNOWN` (H16). Arming both in one `esp_sleep_enable_ext1_wakeup()` call
  fails **silently**: the mismatched source is simply never a wake source, or
  it idles at the trigger level and wakes continuously. Neither raises an
  error.
- **Acceptance:** a host test exercising all three wake sources through the
  same code path with a fake board, including the case where the board reports
  `MotionSensing` as `Degraded`; a design note or ADR update stating the
  accelerometer-only rule so it cannot be silently reintroduced with a
  gyroscope; no application-visible difference between the two boards' wake
  paths.
- **Hardware required:** for the power and latency numbers, yes — `NOT
  EXECUTED — HARDWARE REQUIRED` until measured. For the accelerometer-only
  logic and the host tests, no.

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

### T-106 · Four measurements and five registers, before any cell is ordered
- **Priority:** P1 — it gates the battery decision, and every part of *the
  gate* is cheap: M1, M2, M3 and the five registers need a caliper, a scale,
  plasticine and a console, and no soldering iron. M4's magnetometer leg does
  need one, which is why it is explicitly **not** part of the gate below and
  goes to T-109 if it slips.
- **Dependencies:** the research is done —
  [BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md) and the magnetometer
  datasheet comparison from [#83](https://github.com/hleserg/Attadipa/issues/83)
  ([MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md), which
  landed with [#87](https://github.com/hleserg/Attadipa/pull/87) on
  2026-08-22 — an earlier version of this line called it "not yet linkable").
  What is missing is physical, and only the owner can take it.
- **Why it is not one measurement.** The note's sizing table branches on the
  first three, and each answers a different way of being wrong. The fourth
  answers a separate question — the bus, not the cell — and rides along because
  it needs the same board on the same bench:
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
  - **M4 — the bus scan. The `0x6A` half is DONE**, measured 2026-08-23:
    `0x6A` does not answer, the IMU is at `0x6B`, and
    [HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md)'s IMU row now reads
    `MEASURED` / `VERIFIED` rather than `address CONFLICTING`
    ([WAVESHARE_RUNNING_OUR_CODE](docs/research/WAVESHARE_RUNNING_OUR_CODE.md)
    §3.1). This bullet asked for it as pending for 120 lines after the record of
    it landing, which left two documents in this repository disagreeing about
    whether a hardware fact was established — and *never trust, verify* gives a
    reader no tie-break, so the honest outcome would have been re-running a
    bench session that already happened. Found in review.
    **What is left of M4** ([#83](https://github.com/hleserg/Attadipa/issues/83))
    is the magnetometer half, once the modules are in hand:
    - **Scan one module at a time, and strap `CAD` before scanning.** The AKM
      module *breaks `CAD` out* ([MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md)
      §2.4) — it is not tied to `VSS` by the manufacturer, and this bullet used
      to state that as delivered fact. §4.3 is where the grounding decision
      itself lives. The address table in §2.4 holds two rows only: `CAD` to
      `VSS` gives `0x0C`, `CAD` to `VDD` gives `0x0D`. **Tied high the part
      sits at `0x0D`** — exactly where the QMC5883L is and where the QMC cannot
      move from. What the pin does **floating** is `UNKNOWN`: no internal
      pull-up, pull-down or bias is recorded anywhere in this repository, so
      an unstrapped `CAD` could answer at either address or neither, and an
      operator following the old wording writes down *"QMC5883L confirmed,
      AK09911C absent"* — wrong twice and recorded as `MEASURED`. That is why a
      verified low strap on `CAD` is a **precondition** of this measurement
      rather than part of it: the precondition does not need the floating
      mechanism, it exists because the mechanism is unknown. Found in review.
    - **Neither part is identified by the ACK alone, and the ID registers are
      the opposite way round from what this bullet used to say.** The
      **QMC5883L does** have a chip-ID register — offset `0x0D`, returning
      `0xFF` (§3.4) — and the **AK09911C's `WIA1`/`WIA2` are `UNKNOWN` from a
      primary source** in this repository (§2.4, which closes *"do not copy
      register numbers out of an Arduino library"*). The old sentence had both
      halves backwards and would have sent a driver author to an Arduino
      library for AKM register numbers, the one move §2.4 forbids by name.
      The QMC's ID is not a clean check either: `0xFF` is a valid ID *and* the
      classic signature of an absent device on a floating bus (§3.4), so the
      probe is the address ACK — with one module fitted at a time, which is
      what makes the ACK unambiguous. Found in review.
    - Cheap GY-271 modules sold as QMC5883L are regularly relabelled HMC5883L
      at `0x1E`, so an ACK at `0x1E` and silence at `0x0D` is the relabelled
      part rather than a missing one.
    - **This half needs a soldering iron and parts in the post**, which the
      title of this task does not. `IO15`/`IO14` are bare plated pads
      ([HARDWARE_MATRIX](docs/research/HARDWARE_MATRIX.md#waveshare-esp32-s3-touch-amoled-206)
      "Expansion pad row" — an anchor rather than a line number, because line
      numbers drift on every insertion above them), and `CAD` needs strapping.
      **T-109** already owns the modules, the `CAD` decision and both
      footprints, so if this half slips, it goes there rather than holding a
      P1 battery task open. M1 through M3 do not wait on any of it.
- **And five registers, on the board, whenever convenient:** `0x62` (charge
  current — the one value that has never been read and cannot be quoted from
  the datasheet, because its reset value is eFuse-trimmed), `0x50`, `0x58`,
  `0x12` and `0x69`, at I²C address `0x34`.
- **Acceptance — and it does not include the magnetometer.** M1, M2, M3, M4's
  `0x6A` leg and the five register values, each recorded as `MEASURED` with the
  instrument named, and the sizing table in
  [BATTERY_UPGRADE](docs/research/BATTERY_UPGRADE.md) resolved to one row.
  `UNKNOWN` stays `UNKNOWN` for anything not actually taken. **M4's `0x6A` leg
  is already satisfied** — measured 2026-08-23, recorded in `HARDWARE_MATRIX`
  and `WAVESHARE_RUNNING_OUR_CODE` §3.1. M4's `0x0C` and `0x0D` legs stay
  `UNKNOWN` and are **explicitly outside this acceptance**: they need modules in
  the post and a strap on `CAD`, and a P1 battery gate must not hang on a P2
  delivery. They close under **T-109**, which now owns the strap in its own
  acceptance. So: **M1 through M3 are the gate**, they wait on nothing, and they
  have not been taken.
- **What must not be assumed:** that the sticker settles the capacity. Reading
  it was verified; what it means is exactly what is in doubt.
- **Hardware required:** yes — the board, a caliper, a scale, a bus scan, and
  the five-register read.

### T-111 · A third capability source, for hardware that is neither the board's nor the node's
- **Priority:** P2 — no code depends on the answer yet, but the registry design
  ([ADR-0007](docs/adr/0007-two-capability-layers.md)) is the thing every
  application trusts not to leak where an answer came from, and this is the
  first hardware that does not fit either of the two sources it already knows.
- **Dependencies:** none — this is a design question, answerable on paper. Not
  gated on T-106 or on the modules arriving.
- **Goal:** [#83](https://github.com/hleserg/Attadipa/issues/83) asked this as a
  question rather than answering it, and it stays asked here rather than
  decided inline in a research note. An owner-soldered magnetometer is a
  capability that is a property of neither the board type (other units of the
  same model do not have it) nor an attached Attadipa node (it does not walk
  away). Whether the registry needs a third source class, and how to add one
  **without** letting an application learn which source answered — the
  invariant [ADR-0007](docs/adr/0007-two-capability-layers.md) exists to
  protect — is an ADR question. The lifecycle half of it is already decided,
  and it is decided by
  [ADR-0004](docs/adr/0004-capability-sources.md) rather than by ADR-0007 or
  ADR-0009: `docs/adr/0004-capability-sources.md:186` reads
  "terminal. Nothing may leave it. Ever." of `Availability::Unsupported`, and
  `:198` already reasons about this exact case by name —
  "nothing ever reaches `Unsupported`", because a device that never had a
  magnetometer does not acquire one when a node says so, it acquires a
  *provider*, and that is a different edge. A part the owner solders on is not a
  provider walking up, and it is the case ADR-0004 does **not** cover: so the
  question this task has to answer is what state a per-unit capability sits in
  **before** that specific unit has been probed, given that it may never leave
  `Unsupported` and that an I2C probe finding nothing is indistinguishable from
  a cold solder joint. "Probe at boot" is not by itself an answer to how a
  soldered-on source announces itself.
- **Two more instances of the same shape, both found by
  [#174](https://github.com/hleserg/Attadipa/issues/174) and neither an argument
  on its own for widening the axis — they are here so the ADR has the full set
  in front of it rather than deciding on the magnetometer alone.** First, `Origin`
  has no value meaning *nobody*: a capability neither the board nor a node can
  provide answers `Origin::Local`, which is the only thing two values allow and
  is not what is true. `source()` names that case explicitly and
  `Availability::Unsupported` is documented as the discriminator that says the
  field is not an answer, which is as far as it can be taken without this
  decision. Second, and sharper because it makes an ADR sentence false rather
  than merely coarse: **the phone is already a third source in everything but
  name.** `CompanionLink` and `NotificationRelay` reach `Unprovisioned` and
  `Unreachable` — states ADR-0004 §2's invariant says imply a *remote* provider
  — while reporting `Origin::Local`, because the invariant was written about
  nodes. Which half is wrong is genuinely open: ADR-0002 forbids a phone from
  *providing* a capability, so on that reading the provider is the on-board BLE
  radio and `Local` is right and the invariant is over-stated; on the other
  reading the axis is simply missing a value. `tests/test_capability_registry.cpp`
  names the pair as an exception rather than skipping the states, so a third
  capability entering them is a test failure and lands here.
- **Acceptance:** an ADR, accepted or explicitly deferred with a reason, that
  says whether a third source class exists, what state a per-device (not
  per-board-type) capability is in before that specific unit has been probed,
  what a capability with **no** provider on any side reports, whether the phone
  is on the axis, and how [ADR-0004](docs/adr/0004-capability-sources.md) and
  [ADR-0009](docs/adr/0009-heading.md) are superseded or amended once it does —
  ADR-0004 because the two-source model, the terminal `Unsupported` state and
  the remote-provider invariant are its, ADR-0009 because it is the document
  that assumes heading has no on-board source on either board.
- **Hardware required:** no.

### T-112 · The pedometer has a datasheet now; it still needs someone to walk
- **Filed as [#116](https://github.com/hleserg/Attadipa/issues/116)**, which also
  carries the probe's source and the RAM loader in full — they otherwise exist
  only in a session scratch directory.
- **Priority:** P1 — OD-6 makes the pedometer mandatory and this is the last
  unverified step between the register map and a step count.
- **Dependencies:** none technical. It needs a **person holding the watch**, which
  is the only reason it is not already done.
- **Why now:** the bench session of 2026-08-23 settled *which* datasheet
  describes this silicon — `REVISION_ID = 0x7C`, the QMI8658A `13-52-25` Rev A
  value, against `0x79` for the QMI8658C Rev 0.6 document
  ([WAVESHARE_RUNNING_OUR_CODE](docs/research/WAVESHARE_RUNNING_OUR_CODE.md)
  §3.2). Rev A's chapter 11 documents a complete hardware pedometer; Rev 0.6 calls
  `CTRL8` *"Reserved: Not Used"* and has none. `CTRL8 = 0x90` was written and read
  back exactly, so the register is real. **What has not been shown is that the
  engine counts.** Step count stayed 0 throughout — on a board lying on a desk,
  which is the correct reading and no evidence either way.
- **Goal:** enable the engine and count real steps.
- **What is already prepared:** the probe is written and builds — it loads over
  USB into RAM, writes `CTRL2`/`CTRL7`/`CTRL8` on the IMU and nothing else
  anywhere, prints accelerometer and step count once a second for four minutes,
  and restores the defaults on the way out. Running it is one command; the rest is
  walking twenty steps with the watch in hand.
- **Acceptance:** step count rises with real steps and the count is within a
  sensible fraction of the steps actually taken, recorded in `docs/research/`
  with the number walked beside the number counted. If the engine does **not**
  count, that is equally a result: the step counter becomes firmware on this
  board and the power budget changes.
- **What must not be assumed:** that a matching revision byte proves the feature
  works. It proves which document applies. Chapter 11 still has to be exercised.
- **Also worth one minute while the watch is in hand:** H15's other half — tilt
  the **assembled** watch through known angles and read the raw axes. The board
  frame is silkscreened; the case rotation is not, and one is useless without the
  other.
- **Hardware required:** yes — the owner's unit, and a person.

### T-172 · The upper 16 MB: measure it, or keep leaving it alone
- **Priority:** P3 today, **P1 the moment a layout wants the upper half.**
  Nothing needs it yet — but not because there is room. The vendor did **not**
  fit its table into the low 16 MB. `tools/flash/fixtures/waveshare-vendor-factory.csv`,
  transcribed from the received unit, is contiguous from `0x9000` to exactly
  `0x1000000` with a zero-byte gap — bootloader areas, a 952K voice model, a
  **9 MB** `factory` and one 6 MB OTA slot — and its remaining two rows are the
  ones above the line: `ota_1` at `0x1000000`, which is provably dead, and 6 MB
  of UI assets at `0x1600000`. Three app partitions, not two, and no room to
  spare; that overflow is what this task exists about.

  What Attadipa needs is **UNKNOWN** and cannot be labelled otherwise until an
  image exists to measure. The quantity that matters is not one app slot but
  *two OTA slots plus assets inside 16 MB*, which is the sum the vendor could
  not make — 9M + 6M + 6M is why `ota_1` is where it is.
  `docs/master-prompt-final.md` puts it as *"OTA is not the first MVP blocker.
  But storage/partition decisions must not make it impossible later."* The first
  thing that would force the question is
  [#127](https://github.com/hleserg/Attadipa/issues/127)'s `models` partition.
- **Dependencies:** the rule and its enforcement are already in place —
  `tools/flash/partition_check.py`, run by `ctest` as
  `flash_partitions_below_ceiling`. This task is only the measurement that would
  lift it. Stages 2 and 3 need the owner's unit; stage 3 needs the owner's
  authorisation as well.
- **Why now:** it is written down rather than done, so that whoever wants the
  upper half finds a plan instead of a temptation. The research is complete:
  [FLASH_ADDRESSING_LIMITS](docs/research/FLASH_ADDRESSING_LIMITS.md) traces all
  six flash access paths through ESP-IDF v5.5.5 and gives each a VERIFIED or
  UNKNOWN status. Its finding is that **only `esp_partition_mmap` fails closed**,
  and only since v5.5.5; read, write and erase emit four-byte-address commands
  with no guard, and this part's JEDEC ID `0xC8 0x4019` passes the capability
  gate that might otherwise have stopped them. So the hazard #132 named is real
  and unguarded, and the mitigation is the layout rule rather than the driver.
- **Goal:** move rows 4–7 of that document's table from UNKNOWN to MEASURED, or
  decide out loud that the upper half is not worth the bench time and close this.
- **Acceptance:** §6 of that document run in order, its results written back into
  the same table, and the unit `verify-flash`-clean against the T-099 backup
  afterwards. Stage 2 (read-only, `PURE_RAM_APP`, no flash writes) settles the
  read path and the `mmap` refusal on its own and is worth doing on any bench
  visit. Stage 3 is the only part that writes.
- **What must not be assumed:** that the stub flasher's success above 16 MB
  settles it. The stub runs on this SoC and carries its own SPI routines, so it
  is evidence about the die and the peripheral and not about ESP-IDF's driver —
  the bootloader read `0x0` from an address the stub had just written correctly.
- **The one thing that needs saying before anybody starts stage 3:** there is no
  canary above the line whose 24-bit alias is harmless, because the vendor's
  table is contiguous from `0x9000` to `0x1000000`. The plan picks the least
  valuable alias there is — `0x1FFF000`, whose alias `0x0FFF000` lies 786 KB past
  the end of the image in `ota_0` — and leans on the whole-part backup, which has
  already been used successfully once.
- **Hardware required:** yes for stages 2 and 3. **Stage 3 additionally requires
  a separate owner authorisation**, and must not be run on the strength of any
  earlier one.

### T-113 · Touch needs a reset pulse, and the part number is still a guess
- **Priority:** P2 — the behaviour is understood, which is the part that blocked
  anything. What remains is provenance.
- **Dependencies:** none.
- **Why now:** the FT3168 does not acknowledge on the main I2C bus at all until
  **GPIO 9 is pulsed low then high** — driving it high and holding it is not
  enough, the falling edge is what brings the controller up
  ([WAVESHARE_RUNNING_OUR_CODE](docs/research/WAVESHARE_RUNNING_OUR_CODE.md)
  §3.3). It then reads chip ID `0x64`, firmware `0x02`, vendor `0x11`. That
  confirms the `0x38` address, which this repository had on driver source alone.
- **Goal:** two things the measurement does not give.
  1. **Which part `0x64` denotes.** `0x11` is FocalTech's vendor byte and `0x64`
     is the ID the FT5x06/FT6x36-family drivers expect, which is consistent with
     an FT3168 behind that driver — but no FT3168 datasheet has been obtained and
     the mapping is `UNKNOWN`. Find the datasheet or record that it is not
     obtainable, the way [ADR-0003](docs/adr/0003-radio-not-lora.md) had to.
  2. **The reset pulse belongs in the board layer**, not in an application and
     not in a probe. A BSP that configures GPIO 9 as a high output at init sees
     an empty bus and reports no error, which is the worst kind of wrong.
- **Acceptance:** the part-number question answered or recorded as unobtainable
  with the search documented, and the reset sequence written into the Waveshare
  BSP's touch bring-up with a comment saying why a level will not do.
- **Hardware required:** no for (1), yes to re-verify (2).

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
  - **`CAD` goes to ground** — the *decision*, not a strap anybody has added:
    no module exists yet, so nothing about it is measured. Grounding puts the
    AKM part at `0x0C` and leaves the QST part at its fixed `0x0D`, so both can
    be on the bus at once — which is the only way to compare them in the same
    magnetic environment on the same wrist. **Adding the strap and verifying it
    reads low is work this task owns**, and T-106's M4 hands it here: without
    it the AKM part is not at `0x0C`, and what it does with `CAD` floating is
    `UNKNOWN`. What *was* **measured on 2026-08-23, not predicted:** `0x18`,
    `0x34`, `0x40`,
    `0x51` and `0x6B` answer a bare scan, and `0x38` (touch) answers after its
    reset is pulsed on GPIO 9. `0x6A` does **not** answer — the IMU is at
    `0x6B` — so `0x0C`, `0x0D` and `0x1E` are all free, and so is `0x6A`.
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
- **Acceptance:** the `CAD` strap fitted and **verified low** before any address
  is written down, and each module scanned on its own so the ACK is unambiguous;
  both module footprints recorded as `MEASURED` with the caliper
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
Reason:         The T-Watch S3 Plus is ORDERED, not PRESENT — no physical unit
                to bring up yet. The Waveshare is on the desk and IDENTIFIED —
                silkscreen `ESP32-S3-Touch-AMOLED-2.06`, which is what
                schematic V1.0 describes. Its REVISION is not read: that
                string is the product name and its `2.06` is the panel
                diagonal, so a V1.1 unit would carry it too. Bring-up may
                rely on V1.0 as a document, not as a fact about this board.
Evidence:       OPEN_QUESTIONS A1 (OD-16, issue #54, 2026-08-22). A2 (which
                radio, which GNSS) has an answer, from two sources of
                different strength: SX1262 at 868 MHz is quoted from the
                order listing, and MIA-M10Q is the owner's recollection,
                because that listing names the radio and is silent on GNSS.
                Neither is a marking read off the part, so RadioChip::Unknown
                does not change until the watch arrives and the marking is
                read.

                The GNSS power rail still differs between board revisions (BLDO1
                vs DC3), and **no part marking distinguishes them** — the
                discriminator is a feature of the case: a unit with rear
                BOOT/RST buttons is the DC3-unused revision. See
                OPEN_QUESTIONS D6, the T-Watch rail table in
                HARDWARE_MATRIX (the DC3 row: "unused (was GNSS on earlier
                revisions without rear BOOT/RST buttons)") and VERIFIED_FACTS
                on the GNSS rail, which all say the same thing. Reading SX1262 off
                the radio settles the radio and settles nothing about the
                rail; choosing wrong means GNSS silently never starts, and
                presents as a receiver or antenna fault.
Impact:         Blocks all T-Watch bring-up, every power measurement, the
                whole interference matrix, and any claim that hardware works.
                Does not block Waveshare bring-up, which is unblocked and
                simply not yet done — STATUS.md, the T-010 entry. (This
                previously read "see the M1 section above". There is no M1
                section: the headings are NOW, NEXT, READY, BLOCKED, WAITING
                and DONE, and the nearest M1 above is T-106's closed-case
                clearance measurement, which is a different subject.)
Possible options:
                1. Proceed on simulator and host tests only — no hardware claims.
                2. Wait for the T-Watch to arrive, then make THREE separate
                   observations, none of which substitutes for another:
                   (a) read the radio marking — settles which of the five
                   chips is fitted, and nothing else;
                   (b) look at the back of the case for the BOOT/RST buttons
                   that decide the GNSS rail;
                   (c) establish the BAND. A2's answer is "SX1262 (868 MHz)"
                   and only the chip half of that is readable off a marking:
                   band is set by the matching network and antenna fitted, per
                   OWNER_DECISIONS' own #89 paragraph, and is readable neither
                   over SPI nor off the part. So the 868 rests on the same
                   seller's listing this task refuses for the chip. Note what
                   happens if (c) is skipped: set RadioChip::Sx1262 after (a)
                   alone and radio_info_for() supplies {150 MHz, 960 MHz} —
                   RadioLib's DRIVER limits, not this unit's — so
                   covers(eu868) is true and MeshMessaging goes Ready with
                   nobody having looked at the matching network. The code is
                   not lying; the checklist would be incomplete.
                3. Write the bring-up checklist now so that day one with real
                   hardware is not spent improvising.
Recommended next action:
                Option 3 now, in parallel with option 1. A1–A3 are answered
                (OD-16); nothing further to ask the owner here. A4 is closed,
                not outstanding (OD-14).
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

### T-144 · An agent cannot land a change to `.github/workflows/`, and two fixes are parked behind it
- **Priority:** P1
- **Dependencies:** none. This is a token permission, not a design problem.
- **Goal:** let a fix that has to touch a workflow file actually reach `main`.
```
BLOCKED:
Reason:         Agents here run as `claude[bot]` through the Claude GitHub App
                (`ATTADIPA_AGENT_TOKEN` unset — the documented default,
                CLAUDE_AUTOMATION.md), and that installation token holds no
                `workflows` permission. Every push touching
                `.github/workflows/` is refused by the remote, so any finding
                whose fix lives in a workflow can be written, tested and
                reviewed here, and cannot be delivered.
Evidence:       Verified 2026-08-24 on a scratch branch, one character changed:
                  ! [remote rejected] probe/wf-push-170 -> probe/wf-push-170
                    (refusing to allow a GitHub App to create or update
                     workflow `.github/workflows/pr-merge-sweep.yml` without
                     `workflows` permission)
                Two fixes are parked on it, both against the same file:
                  · #170 — docs/automation/pending/170-merge-sweep-completeness.patch
                    (the merge sweep proving it read the whole pull request)
                  · #130 — docs/automation/pending/130-merge-sweep-caller.patch
                    on pull request #154, which files this same blocker as
                    "T-127" — a number already taken by the anchor check in
                    DONE. Whichever of the two lands second should fold its
                    entry into this one rather than leave three numbers for
                    one problem.
                Apply both **in one commit**, not in either order: they edit the
                same workflow. `merge-candidate-test.sh` hard-fails CI for every
                open pull request if #154's patch lands first — but **not** in the
                other direction: its state machine keys on the 170 patch, and
                #154's is on that pull request rather than in `pending/`, so
                landing 170 alone goes green while leaving #154's patch stale.
                The order this entry recommends is the unguarded one. Each `git rm`s only its own file, so neither deletes the
                other's while it is still parked.
Impact:         #170 is closed fail-closed rather than fixed: until the patch
                lands, `merge-candidate.sh` refuses the pre-#170 nine-argument
                caller by arity, so the sweep merges NOTHING and logs the file
                to apply once per open pull request per run. Correct, and not
                finished. Everything still merges through an orchestrator
                session, which is unaffected.
                The two patches touch the same file and will conflict with
                each other, not with `main`.
Possible options:
                1. An orchestrator session applies both patches in one
                   commit, resolving the one overlap by hand, and `git rm`s
                   the two patch files it applied — not the directory, which
                   also holds README.md, the target of three links in
                   APPROVAL_STALLS.md. Costs one live session.
                2. Owner grants the Claude GitHub App `workflows: write` on
                   this installation. Fixes the class, not just these two —
                   and widens what an agent may change to include the files
                   that decide what agents may do.
                3. Owner sets `ATTADIPA_AGENT_TOKEN` to a fine-grained PAT
                   carrying `workflows`. Same reach as 2, and it also changes
                   the author of every agent commit to the token's owner.
                4. Leave both parked. The merge sweep stays a no-op, and the
                   next workflow-level finding parks behind these two.
Recommended next action:
                Option 1 for these two, then decide 2 or 3 at leisure. It
                needs no permission change and no new trust boundary, and the
                sweep is back the same day. Options 2 and 3 hand an agent
                write access to the workflows that constrain agents, which is
                exactly the "a gate that can widen itself is not a gate"
                argument CLAUDE_AUTOMATION.md makes about `docs/automation/`
                — worth doing deliberately, not as a side effect of clearing
                a queue.
```

---

## WAITING

### T-012 · Answers from the project owner
- **Priority:** P0
- **Waiting on:** the project owner
- **Questions:** [OPEN_QUESTIONS](docs/research/OPEN_QUESTIONS.md) A6 —
  whether the node carries a magnetometer. **A1–A3 are no longer on this
  list** — answered 2026-08-22 on
  [#54](https://github.com/hleserg/Attadipa/issues/54), recorded as
  [OD-16](docs/research/OWNER_DECISIONS.md#od-16--a1-a2-and-a3-no-watch-yet-sx1262-confirmed-by-listing-and-three-meshcore-nodes-instead-of-one).
A1's schematic-revision
  sub-question is **not closed and is not owed by the owner** — the silkscreen
  reads `ESP32-S3-Touch-AMOLED-2.06`, which is the **product name** schematic
  V1.0 describes, not a revision field: `2.06` is the panel diagonal, so a V1.1
  unit carries it unchanged (`WAVESHARE_BOARD_RECEIVED` §1.1, and
  `OPEN_QUESTIONS` D20, where it is now filed). An earlier version of this
  bullet called it closed, which was the same mistake the rest of this branch
  exists to correct. A2's marking-read-off-the-part confirmation likewise
  remains, and is a hardware-in-hand task now, not an owner question. **A11 is new and IS on this list** — one T114 with GNSS or two,
  [#124](https://github.com/hleserg/Attadipa/issues/124). **A5 is no longer on it either** — 2026-08-22, the owner has
  ordered a CJMCU-9911 (AK09911C) and a GY-271 (QMC5883L) and is soldering one
  in ([#83](https://github.com/hleserg/Attadipa/issues/83)). A6 is untouched by
  that: a node's magnetometer and a wrist's magnetometer answer different
  questions. **A4 (the regulatory region) is no longer on this list** — closed
  2026-08-22, not by an answer but by the owner declining to give one:
  legality is his problem, not the firmware's
  ([OD-14](docs/research/OWNER_DECISIONS.md#od-14--which-region-is-the-owners-problem-not-the-firmwares)).
  No task here researches a specific jurisdiction's rules on the project's own
  initiative; [ADR-0006](docs/adr/0006-settings-and-bounded-values.md)'s
  transmit-closed-while-`Unknown` gate needs no such research to keep working.
- **Impact:** A5 and A6 decide whether five magnetometer epics are dormant or
  dead, and A6 does **not** give the watch a compass even if the answer is yes
  ([ADR-0009](docs/adr/0009-heading.md) §3). A5's answer moves those five epics
  from *possibly dead* to *dormant with a delivery date*, and hands
  [ADR-0009](docs/adr/0009-heading.md) a second possible provider for heading —
  see [MAGNETOMETER_RETROFIT](docs/research/MAGNETOMETER_RETROFIT.md). A2's
  answer, now given, decided the other half: of the five candidate radios two
  cannot do LoRa at all and only one is supported by the pinned MeshCore
  ([ADR-0003](docs/adr/0003-radio-not-lora.md)), and the order listing says
  SX1262 — so the watch has a local mesh path, subject to reading the marking
  off the physical part when it arrives.
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

### T-127 · A link's `#anchor` is captured and then never checked — **DONE** 2026-08-23
- **Priority:** P3
- **Dependencies:** none.
- **Goal:** `tools/docs/check_docs.py` check 1 matches a link with an anchor on
  it, captures the anchor as a regex group, and then tests only
  `os.path.exists(target)`. So every `#od-16--...` style deep link in this
  repository is unverified, and a heading renamed or renumbered leaves a link
  that resolves to the top of the right file — which reads as working. This is
  the half of the OD-16 collision that the new duplicate-decision check does
  **not** cover: that one catches two headings with one number, this one catches
  a citation pointing at a number that has moved.
- **Acceptance:** anchors resolve against the target file's own headings, using
  GitHub's slug rule (lower-case, drop punctuation except hyphen and
  underscore, spaces to hyphens, de-duplicate with `-1`, `-2`). Mutation tests
  in `tools/docs/test_check_docs.py` for: a good anchor, a renamed heading, an
  em dash, backticks and bold inside a heading, two headings that slug the same,
  and an anchor into a file that has none.
- **Watch for:** the first run will find pre-existing broken anchors. That is
  the point, but it makes this a two-commit job — the check, then the fixes —
  and the fixes are the larger half. Do not weaken the rule to make the first
  run green.
- **Hardware required:** no.
- **What came of it:** `check_links` now resolves every `#anchor` against the
  target document's own headings, including `](#same-document)` links, which the
  link pattern had never captured at all. GitHub's slug rule is implemented as
  described — and its awkward half is that punctuation is dropped while the
  spaces around it are not, which is why `## OD-16 — A1, A2 and A3` answers to
  `#od-16--a1-a2-and-a3` with two hyphens. Headings inside a fenced block are not
  headings. Anchors on non-Markdown targets are left alone: GitHub anchors a code
  file by line, and reporting `#L12` would be noise. Seven mutation cases.
  **The first run found exactly one broken anchor in the whole repository** —
  `PEDOMETER_PARTS.md:19` had dropped the trailing `` `SUPPORTED` `` from the
  slug — so the two-commit worry did not materialise, and the fix is in this
  commit. The OD-16 half is what this was for: renumber one of two colliding
  headings now and every `#od-16` link says so.

  **And it caught this record while it was being written.** The paragraph above
  described the syntax of a same-document link, in backticks, and the new check
  read the illustration as a live link into a heading that does not exist —
  which is the `EXAMPLE.md` defect exactly, one check over. GitHub renders an
  inline code span as characters, so `check_links` now blanks code spans before
  looking for links. Two more cases: an illustration stays quiet, a real link
  after a code span on the same line is still read.

### T-149 · The generated asset checks never looked at the generated bytes — **DONE** 2026-08-23
- **This task was filed as T-127 and renumbered on merge.** `main` took its
  own T-127 — *a link's `#anchor` is captured and then never checked* — while
  this branch was open, so two unrelated tasks arrived at one ID. `main`'s
  keeps the number because it landed first; every reference here to the
  *anchor* half still reads T-127 and is correct. `check_docs.py`'s duplicate
  task-ID check would have caught the collision at merge, which is what it is
  for; it is recorded here so the next reader does not read the two as one.
- **The finding, reproduced before it was fixed** (issue #69). Both committed
  asset trees were guarded by a stamp of their *inputs* and then a count of
  filenames. Editing a line of a generated font left
  `generate_ui_fonts.py --check` at exit 0 saying *"fonts: inputs unchanged, 4
  generated file(s) present"*; changing the first A8 bitmap byte of
  `attadipa_icon_mesh_33.c` left `generate_images.py --check` at exit 0 saying
  the same about ten. Missing glyphs, altered masks and corrupted descriptors
  could all reach firmware behind a green CI run.
- **The second half was worse, and is the reason the first was never caught.**
  `lv_font_conv` writes its own argv into an `Opts:` comment, so all four
  committed fonts carried `/mnt/e/projects/firefly/...` — one machine's absolute
  paths. A fresh generation anywhere else differed in bytes while being
  identical in every glyph, so the only byte-for-byte gate available reported
  **all four files as differing** and could never be turned on. MEASURED here
  before the fix: four false positives, and the diff was one line per file.
- **One contract for both trees**, `tools/integrity/stamp.py`: `inputs` plus a
  `output <sha256> <name>` line per committed file, strict parser, three
  distinguishable verdicts — inputs moved, a file changed, the stamp itself is
  damaged — because those need three different repairs. Written atomically and
  **only by a generator**; there is deliberately no "re-stamp what is on disk"
  mode, since a tool that blesses whatever bytes it finds is the same hole
  wearing a maintenance hat. Reuse considered and recorded: `sha256sum -c` was
  the close candidate and is in [REUSE_LEDGER](docs/research/REUSE_LEDGER.md).
- **The provenance line is normalized** to logical paths, and now says something
  a reader can check: every generated font banner carries the source TTF's
  SHA-256 and the pinned converter version, and the generator **refuses a
  converter whose `--version` is not 1.5.3** rather than trusting whatever npm
  left on PATH. The glyph bytes did not move — verified by comparing the bodies
  past the header, all four identical.
- **45 mutation cases**, `ui_generated_outputs_reject_mutations`, needing neither
  Node nor Pillow so they run in the same host job as the gate they are about.
  Each of the fourteen outputs is corrupted in turn in a copy of the tree; so is
  each input, and the stamp in six different ways. A control case at each end
  asserts an untouched tree still passes — that is what caught a harness bug
  where CPython reused bytecode from a mutation because the restored source had
  the same size and the same mtime to the second.
- **The expensive half is a script that is run and not yet automated** —
  `tools/integrity/reproducibility.py`, T-128 for the CI job: fetch Montserrat
  from the pinned LVGL commit (one 243 kB file, hash-checked, instead of the
  350 MiB clone), install `lv_font_conv@1.5.3`, regenerate **both** trees from
  two different absolute paths and compare all sixteen files against what is
  committed. Host jobs stay Node-free, which is the whole reason the outputs are
  committed. Run here before the job existed: 16/16 identical, 3.6 s.
- **Not hardware.** Whether the glyphs and masks look right on a panel is
  `NOT EXECUTED — HARDWARE REQUIRED` and belongs to a HIL task; this is about
  the bytes being the bytes that were generated.

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
- **The stored format is settled, not inferred**: each image is exactly
  **411 652** bytes = a **12-byte header** plus **410 × 502 RGB565
  little-endian**. Header is `u32` magic `0x00001219`, `u16` width, `u16` height,
  `u32` stride (820 = width × 2). `12 + w·h·2` equals the file length exactly.
- **The on-disk byte order was settled by rendering**, which is the only thing
  that could settle it: little-endian gives coherent artwork, big-endian gives
  noise.
- **`tools/flash/spiffs_extract.py`** does the extraction without `mkspiffs` and
  without an ESP-IDF build — the blocker the original task named. `strings`
  recovers a SPIFFS image's names and none of its bodies.
- **Feeds T-034**, and the distinction matters: the vendor's header is not a
  hardware fact and is worth noticing rather than copying — it has width, height
  and stride but **no format field**, which is the field needed the moment a
  second format exists. Three full frames cost 1.18 MB.
- **Corrected 2026-08-23, and the correction is the point of the entry now.**
  This record used to continue *"the panel's pixel format and byte order are
  facts about the hardware"*. The pixel **format** half stands. The byte
  **order** half was an inference across a boundary the render never crossed, and
  the one display path readable in pinned source **swaps every pixel** before
  transfer (`.swap_bytes = 1` → `lv_draw_sw_rgb565_swap()` → `tx_color()`
  verbatim). The transfer order is now **`UNKNOWN`**, registered as **D21**, with
  two routes to close it in
  [WAVESHARE_FLASH_LAYOUT](docs/research/WAVESHARE_FLASH_LAYOUT.md) §7. Nothing
  shipped is wrong — T-034 emits `A8` masks, which have no byte order — but the
  first line of display bring-up must not read its answer off this task. (Nor
  must the first colour **asset** read it off D21: an asset's byte order follows
  LVGL's framebuffer format, and the wire order is absorbed at flush. Corrected
  in review; see VERIFIED_FACTS.) Issue
  [#109](https://github.com/hleserg/Attadipa/issues/109).
- **Two findings outside the task's scope**, both recorded in
  [VERIFIED_FACTS](docs/research/VERIFIED_FACTS.md): the music gives T-105 a
  strong prior that `AAC210602A1` is the speaker, and the factory image carries
  third-party all-rights-reserved audio, which is a second reason the dump never
  goes near this repository.
- Extracted files and rendered PNGs are **not committed**. The extractor and the
  measurements are.


### T-102 · Documentation consistency in CI — **DONE** 2026-08-22
- `tools/docs/check_docs.py`, run by the `Documentation consistency` job.
  Eight checks: relative links, inline code spans, task IDs, owner-decision
  numbers, task bodies, root files, `file:line` citations, and open-question
  IDs. Four at first,
  each of a failure that had already happened here. **A fifth was
  added 2026-08-23** — nothing unexpected is tracked at the repository root —
  after `git add -A` swept a scraped vendor page into `main` through
  [#98](https://github.com/hleserg/Attadipa/pull/98); a sixth and a seventh
  followed on the same day, for the four pull requests that each claimed OD-16
  and for citations that land on a real, wrong line.
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
  **An eighth check landed 2026-08-24**: one open-question ID names one
  question. Check 4 does that for OD numbers in `OWNER_DECISIONS.md` and
  stopped there; `OPEN_QUESTIONS.md` carries about four times as many
  identifiers and had nothing. A branch filed the panel's wire byte order as
  `D19` while `main` took `D19` for the display-FPC part marking, the branch
  merged `main`, and nineteen citations in eight files then pointed at two
  different questions with CI green. Struck rows count — a retired number is
  spent, not free. Found in review of
  [#152](https://github.com/hleserg/Attadipa/pull/152).
  **74 cases** in `tools/docs/test_check_docs.py`, several of which assert the
  checker does *not* fire where firing would be wrong — a `###` sub-heading is
  not a second decision, a range straddling a blank line is how a table is
  cited, and a line number in somebody else's tree is not ours to verify. The
  suite prints its own count, so this number has one source. One reproduces the
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
  regenerated. What it did **not** cover was the generated bytes themselves, so
  a hand-edited mask passed — **T-149** closed that, and `INPUTS.sha256` now
  records a hash per output as well.
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
- **A prerequisite that was closed on an unproven fact, reopened 2026-08-23.**
  T-103 told this task the panel's byte order was settled; it was not — only the
  vendor *files*' on-disk order was, and D21 now holds the real question. **This
  task is unaffected in fact**: every asset it emits is `LV_COLOR_FORMAT_A8`
  (`tools/assets/generate_images.py:168` "--cf"), one byte per pixel, no byte order to
  get wrong — so `DONE` is still honest and no output needs regenerating. It is
  **not affected in inheritance either, and an earlier version of this bullet
  said it was.** *"The first task to add a colour format inherits D21 and must
  take the swap setting from a datasheet or a measurement"* was the instruction
  this correction exists to withdraw, and it survived here — 2 100 lines from
  the bullet that corrects it, in the entry an agent picking up T-034a reads to
  find out what it inherited. An asset's byte order follows LVGL's colour-format
  contract and the framebuffer the software renderer writes into; the wire order
  is absorbed once at flush by the port's `swap_bytes` flag, which is a **board**
  fact living in `boards/`/`platform/`. For `RGB565A8` the old instruction was
  not even executable: the vendored converter has no swapped variant of that
  format. Found in the second review round of
  [#152](https://github.com/hleserg/Attadipa/pull/152). Issue
  [#109](https://github.com/hleserg/Attadipa/issues/109).

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
  one file exempted for holding the palette; `tools/ui/selftest.py` proves the
  checker rejects thirty real mistakes, accepts twenty-eight correct forms
  and reports three diagnostics usefully. `sim/boot_screen.cpp` no longer
  contains a hex colour or a raw padding.
- **The acceptance criterion was weaker than it read, and is now what it says**
  — [#68](https://github.com/hleserg/Attadipa/issues/68), fixed 2026-08-23. The
  checker matched one physical line at a time against a hand-written list of
  setter names, so wrapping a call across lines changed the verdict and
  `lv_obj_set_size`/`lv_obj_set_pos` were never on the list at all. It now blanks
  comment and string bodies, takes each call whole by balancing parentheses, and
  judges arguments by position against an inventory read out of the pinned LVGL
  v9.5.0 headers. **A bump of the LVGL pin has to re-derive that inventory** —
  the step is recorded in [DEPENDENCIES](docs/research/DEPENDENCIES.md), and why
  it is a list rather than a parse is in
  [REUSE_LEDGER](docs/research/REUSE_LEDGER.md).
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
- **Goal:** `waveshare/esp_lcd_sh8601` is a fork of `espressif/esp_lcd_sh8601` —
  its own files carry Espressif's SPDX headers. At `:280`, inside
  `panel_sh8601_draw_bitmap`, `tx_color(...)` is called bare and the function
  then returns `ESP_OK` unconditionally: **a failed frame transfer is reported as
  success.** Present at the two revisions read — `694ece03` (2023-11-03) and
  `5d75f3f0`, still bare — and fixed by `e5b9295a`. **Which released component
  versions those correspond to is not derived**; this bullet said *"present in
  1.0.2, which the published demo pins, and in 2.0.0"* until the fourth review
  round of [#152](https://github.com/hleserg/Attadipa/pull/152), which is the
  same inference-from-commit-count the *Sharpened* bullet below already
  withdrew for its neighbour, and the version strings do not identify whose
  versioning they are — the fork pins `==1.0.2` and upstream has a `1.0.2` too.
  Espressif ships both an unforked `esp_lcd_sh8601` and a purpose-named
  `esp_lcd_co5300` — QSPI, accepting a custom init table — under the same
  Apache-2.0. Take the pin map and the init table; depend on upstream.
- **Sharpened 2026-08-23, and one premise withdrawn.** The unchecked call is
  **not** a fork divergence: upstream carried it at the same line 280 from
  `694ece03` (2023-11-03) until `e5b9295a` (2025-12-10), where the changelog
  records *"Fix draw_bitmap not propagating tx_color errors"* for **`v2.0.1`**.
  So the fork inherited it. The task's conclusion is unchanged and better
  evidenced — upstream **has** the fix and the pinned fork does not — but the
  "two-line fork" count was derived against *today's* upstream and must be
  re-derived against the revision the fork was taken from before it is quoted
  again. Also: upstream lives in `espressif/esp-iot-solution`, not `esp-bsp`.
- **Evidence:** [WAVESHARE_ARRIVAL.md](docs/research/WAVESHARE_ARRIVAL.md) §3.3,
  including the 2026-08-23 correction block.

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
