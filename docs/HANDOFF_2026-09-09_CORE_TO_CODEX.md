# Handoff — core, hardware and research → Codex

Written 2026-09-09 by the Claude session that held the core/hardware/research
zone. It hands over that whole zone, not one task. Read §0 and §2 before
touching anything: one pull request is open mid-review and carries a result
that exists nowhere but this document.

It is written once and **it is not a ledger** — when a fact here stops being
true, the issue, the pull request or `AGENTS.md` is where it gets corrected,
not this document. §0 in particular is a snapshot of the hour it was written.

---

## 0. What is in flight this minute

| What | State |
| --- | --- |
| **PR #494** (`claude/s17-artefact-classification-491`, `Fixes #491`) | **OPEN**, head `097aecbc`, `ai-review:blocking`, MERGEABLE. Review **round 3 of a 5 round ceiling**, floor 4. Four open findings, all `normal`, all holding. |
| PR #505 | **OPEN**, not a draft, `claude/forget-confirmation-truth-504`, `Fixes #504`, `ai-review:blocking`. Not mine, not started by me. |
| PR #501 | yours (Shell Study 03), `ai-review:blocking`. |
| Writer lease | **released.** `git ls-remote origin 'refs/tags/attadipa-claims/writer'` is empty. Nothing of mine is holding the gate. |
| Working tree | clean on the tracked side; this file is the only addition. |

**Nothing is half-edited.** Every commit I made is pushed. If you do nothing
at all, the repository is consistent — #494 simply sits blocked.

---

## 1. The zone split, as the owner set it

Three agents, cooperating rather than racing. The owner's words:
«гонку с Codex так не выиграть — а давай ка вы будете общаться и
договариваться а не гоняться», and later «я параллельно ввожу еще агента,
задание то же сотрудничать и общаться всем троим».

- **You (Codex)** — visual language, screens, UX, animation, mockups. The
  design system, the studies, the faces.
- **Third agent** — physical sensors.
- **Me, now yours** — core architecture; the Position / Heading / Navigation
  contracts; GNSS semantics; MeshCore integration; BLE contracts;
  trust, validity and provenance; the hardware abstraction boundary;
  production firmware integration; bench measurement provenance.

Layering, which nothing may short-circuit:
`Hardware → Drivers → Core contracts → Services → Applications → UI`.

The product goal is one sentence and it outranks elegance:
**companion GNSS → BLE → watch → heading → navigation arrow.**
«Не идеальная архитектура. Работающее устройство на руке.»

Rule between agents: **«Не переписывай их решения без доказательств».** I have
been keeping that in both directions. `ui/lvgl/provision_face.cpp` is *your*
file; `ui/lvgl/mesh_face.cpp` is mine and is now yours. I edited
`provision_face.cpp` twice, disclosed both on #469 with the diff before they
landed; one changed code and one was comment text only, and the offer to
revert either still stands (see the correction on #488).

The mission that decides ties, in priority order:

1. it works on the device;
2. it does not lie about its own state — `NoFix != stale`, `stale != current`,
   `unknown != valid`; never show `0 m`, `(0,0)` or an arbitrary direction for
   a value that is not known;
3. it preserves the boundaries above;
4. it is tested;
5. it is minimal;
6. it generalises.

---

## 2. PR #494 — where I stopped, and why it matters

The pull request's thesis: the S17 power entry excluded 293 of 242 847 samples
on the claim that `1.27 A` is *physically impossible* on a 5 V USB line. It is
not impossible — it is a magnitude the front end permits — so the exclusion had
to be re-justified rather than assumed. That thesis is intact and unchallenged
through three review rounds.

I picked #494 up from `app/claude`, which had abandoned it, by claiming the
writer lease **by pull request number** rather than by issue — see §5.

**Two rounds of budget remain (4 and 5).** Past round 5 the ceiling makes
`ai-review:pass` mean "the review ended", not "the review passed".

### The four open findings, all verified against the file by me, all real

| id | what it is | the fix |
| --- | --- | --- |
| `every-number-is-a-figure-of-the-retained-set` | The entry says every number in it is a figure of the retained 242 554. Four of its own figures are not — two are full-set means, and the excluded-set counts are figures of the 293. | One word: "every **figure above**". |
| `sensitivity-not-derived-from-published-means` | The entry publishes "1.8 mW, 0.23 %" for the exclusion's effect on the mean. The four-decimal means it also publishes give **1.7363 mW, 0.2229 %** — the pair was subtracted from the *rounded* figures. | Restate as 1.7 mW / 0.22 % to the entry's own precision. I confirmed the arithmetic. |
| `pmu-retention-uncited-for-this-board` | "the PMU holds its registers across an ESP32 reset" is uncited, and its home in this repository (`docs/research/BATTERY_UPGRADE.md`, the `REG 0x62` doctrine) derives it for the **Waveshare** board and **conditions it on the cell being connected**. S17 is the T-Watch, whose cell state this same entry records as never established. | Cite it with its precondition, or drop the analogy. No published figure depends on it. |
| `counts-declared-not-executed` | The entry publishes class counts from the pinned capture and then declares them **NOT EXECUTED**, on the stated ground that `~/attadipa-bench/` is unreachable from the editing machine. That ground is false for the three commits that produced those counts. | See §3 — this one is no longer a wording fix. |

### What I would have written next, so you do not have to re-derive it

The fixes for the first three are mechanical and I had verified all the inputs.
The fourth is not, because of §3.

---

## 3. An executed result that is published nowhere

**This is the part that dies if this document is not read.** The entry contains
a bullet headed *"What would settle the classification needs the capture, not
the bench, and it is NOT EXECUTED"*. It names two tests. **Both are runnable on
this bench, and I ran them minutes before handing over.** Nothing of this is in
the repository or in any pull request comment.

Inputs, both present in `~/attadipa-bench/`:

- `twatch_taper_20260908.csv`, sha256
  `0062e49452b5e647c0b236a9260d74362a6c817309da91b45e9124373b9b4dac` —
  **matches the sha256 the entry already pins**, checked before any figure.
- `fnirsi_logger.py`, sha256 `388061aeb580cde0b7306626d87f833dfdbbf1fdf6c17663b49fde557ace250b`
  — **matches the `388061ae…` copy the entry names**, so the copy's identity is
  no longer `UNKNOWN`.

### Result A — the report-grouping test

The logger packs four samples into one 64-byte HID report and writes each
sample's slot as column 2 (`sample_in_packet`). The entry's own stated test:
*transport corruption of a report shows up as a run inside that report, so do
the 293 group into roughly 73 reports, or scatter across 293?*

Grouping is by the slot column alone, over **all 242 847 data rows** — a new
report starts wherever the slot fails to increase:

```python
import collections
# The file is whitespace-separated despite the .csv name: csv.reader on it
# returns one empty field per line and every index below fails.
rows = [l.split() for l in open(path) if l.strip()][1:]   # header dropped

g = collections.defaultdict(list); rep = -1; prev = None
for r in rows:
    s = int(r[1])                   # column 2, sample_in_packet
    if prev is None or s <= prev:
        rep += 1
    g[rep].append(r); prev = s
def excluded(r):                    # the entry's own filter
    V, I = float(r[2]), float(r[3])
    return not (4.0 < V < 5.5 and 0.0 <= I < 1.0)
```

```
rows 242847
reports 60712      size histogram {3: 1, 4: 60711}
excluded samples 293 in 293 distinct reports
```

The one short report is **the tail** (index 60711, the last), which is a
capture that stopped mid-report and not a hole in the middle. “Excluded” is
`excluded()` above — the entry's own structural filter, nothing new.

**They scatter. One excluded sample per report, 293 reports, never two in the
same one.** That rejects **the clustering model the entry itself proposed** —
corruption making the excluded samples group into roughly 73 reports. It is not
a proof of transport integrity, and it does not rule out a transport bit or
field error touching a single sample inside an otherwise sound report. The CSV
keeps no report bytes and no checksum, so that narrower case cannot be tested
retrospectively at all.

### Result B — the overlap test

```
current-only samples: 72
  sharing a report with a structural exclusion: 0
  alone in their report:                       72
```

**None** of the 72 `≈1.27 A` samples shares a report with a structural
exclusion (`V = 0` or a binary-round voltage).

**This is a corollary of Result A, not independent corroboration of it**, and
saying otherwise would double-count one measurement. A puts 293 exclusions in
293 distinct reports, so by pigeonhole no report holds two; the 72 are a subset
of those 293; therefore none of them can share a report with another. B is
worth stating because it is the form the entry's question took, not because it
is a second piece of evidence.

### Result C — the pinned copy

`--crc` is `default=False` in the pinned copy, and the decode is
`offset = 2 + 15 * i` inside `for i in range(4):`, exactly as the entry
describes. So the entry's description of the copy is **verified**, and the
`UNKNOWN` about whether the copy matches upstream in these two respects can be
closed for the copy's own content.

### What this is, and what it is not

It **strengthens** #494's thesis: the excluded samples are not report-shaped,
so they are not report-level transport corruption. Publishing it converts a
`NOT EXECUTED` bullet into a measured one and answers
`counts-declared-not-executed` properly rather than by re-wording.

Be precise about the limit, or you will overclaim exactly the way this pull
request exists to stop: this refutes corruption **of a whole report**. It does
not exclude per-sample corruption inside a report, and because `--crc`
defaulted to `False` nothing verified any checksum during the run. Whether the
run itself passed `--crc` was never recorded and stays `UNKNOWN`.

This is a **software** result computed from a pinned capture. It is not a
hardware `PASS` and must never be labelled one.

---

## 4. Mechanisms that will bite you

Learned the hard way in this session; each has cost real time.

1. **The writer lease is for editing only.** One repository-wide slot. Claim,
   edit, push, `finish` immediately. Never hold it across CI or review.
   `.github/scripts/writer-start.sh start REPO ISSUE AGENT_ID` from current
   `main`. `AGENT_ID` is an opaque label published in a tag anyone can read —
   **never a credential**.
2. **Check `$?` directly, never through a pipe.** Piping
   `writer-start.sh start …` into `tee` gives you the pipe's status, not the
   claim's. That has bypassed the lease once and hidden four broken citations
   once.
3. **`writer-start.sh finish` lies.** It reports the tag "still exists after
   DELETE". That is a ref-cache artefact. `git ls-remote origin
   'refs/tags/attadipa-claims/writer'` is the truth.
4. **Recover an abandoned PR by its number.** At the 4-PR limit a fresh claim
   is refused, but `.github/scripts/writer-admission.sh` admits a claim on an
   **existing PR number** as `recovery / existing-pr` before the width check.
   This is not a way around the limit: an open PR is already counted in the
   width, so picking it up adds none. The check is on the number, not on who
   owns it — so claim only a PR nobody is working. That is how #494 was picked
   up.
5. **A push during a live review is free.** The review workflow sets
   `cancel-in-progress: true` on a per-PR concurrency group, and
   `.github/scripts/review-verdict.sh` advances `round=N` **only when a
   findings block was actually published**. Three cancelled runs on #494 left
   the ledger at `round=2`. So the moment you know the pushed head is wrong,
   fix and push — waiting spends a round reviewing text you are about to
   delete.
6. **The review caps at five rounds.** Past the ceiling `ai-review:pass` means
   the review *ended*. Read `round=N` out of the ledger comment — it is PATCHed
   in place, so counting comments tells you nothing.
7. **`CLEAN` is not `approved`.** Mergeability is not a verdict. Read the
   `ai-review:blocking` label.
8. **Citations carry their quoted text.** `check_docs.py` matches the quote, so
   a citation line must never be wrapped — breaking it breaks the quote. In
   `VERIFIED_FACTS.md` the convention is ≤79 columns for prose with citation
   lines left whole; measure in **characters**, not bytes — `–`, `×` and `−`
   are multi-byte and `awk length()` over-reports.
9. **Run `check_docs.py` in a real `git worktree`.** A `git archive` export has
   no `.git`, which silently turns the fingerprint rule off and greens the
   check.
10. **A `VERIFIED_FACTS.md` insert moves other files' citations.** A 57-line
    insert broke 11 of them. Repoint by relocating each citation's own quoted
    text — never by adding a line delta, and never with difflib, which carries
    staleness forward.
11. **Two green PRs can still break a citation.** `check_docs` reads one tree;
    the breakage lands on the second merge.
12. **Stage merge conflicts by name.** `git add -A` during a merge here commits
    about 4 500 generated files.

---

## 5. The queue, ranked

**Do these first — they are the product, not the paperwork.**

1. **#450** — the wearable navigation vertical slice: companion coordinates
   reach the wrist. This is the goal the owner stated. Its magnetometer half
   belongs to the third agent; the contract seam is mine and is now yours.
2. **#490** — the second half is unfinished: a battery reading on the watch.
   Nothing parses `RESP_CODE_BATT_AND_STORAGE` yet and each board's
   `getBattMilliVolts()` return is still `UNKNOWN`. The `VERIFIED_FACTS.md` row
   belongs to that implementation, not to a research PR.
3. **#494** — finish it. §2 and §3 have everything.
4. **#498** — a bound node is credited with `Position` before it has ever
   reported one. This is a direct honest-validity violation and it is filed.
5. **#442** — the watch's own GNSS reaches `LocationService`; the outdoor half
   is not done.

**Filed by me, unassigned, all real, none urgent:** #502 (three
`provision_face.cpp` findings, **yours**: 2.19:1 contrast measured by eye on
both geometries, 84 px column overflow at 240×240, dead `surface`/`raised`),
#503 (a deferred review finding is promised a follow-up issue and never gets
one), #500 (a printf/pipefail EPIPE flake, 30 instances under `.github/`),
#499 (three inert launcher-gate findings).

**Waiting on you:** the `NavText` frame-sentence field, an ADR-0009 §1 gap that
needs Study 02's wording (#486).

**Waiting on the owner or on hardware:** #472, #471, #467, #316 all carry
`needs-owner`. #450 and #442 carry `needs-hardware`. The T-Watch charge-state
discriminator needs the owner's finger on the device.

**Smaller, honest debt I did not get to:** `VERIFIED_FACTS.md` calls `A4`/`A6`
"open stubs" where the D23 addenda says NO STUB; #462 (23 of 71 source-comment
citations are stale, and `check_docs` walks `.md` only so CI cannot see them);
#304; #478.

---

## 6. Non-negotiables — these are not style

- **No hardware fact without provenance.** Datasheet, schematic for the right
  board revision, vendor source, or a reproducible bench result. Otherwise
  write `UNKNOWN` and record the blocker under `docs/research/`.
- **A test that did not run on physical hardware is never a hardware `PASS`.**
  Write `NOT EXECUTED — HARDWARE REQUIRED`. Label numbers `MEASURED`,
  `ESTIMATED` or `UNKNOWN`.
- **Do not burn eFuses**, enable irreversible security settings, destroy keys,
  or commit secrets. Reversible flashing only, with a verified factory backup.
- **No GNSS config-save may be sent to a bench module.** RAM-layer
  `CFG-VALSET` is permitted; `UBX-CFG-CFG`, `$CFGSAVE`, ALLYSTAR `06 09` and
  the BBR/Flash layers are not. If `UBX-CFG-RST` is ever sent, `navBbrMask`
  must be `0x0000`.
- **Never probe `REG 0x62` by writing high codes.**
- GPIO 6 (DIO3) is never driven as an output. GPIO 45 backlight: never add a
  pull-up.
- **Do not touch the third agent's ESP32 dev board** — it is the CH343 bridge
  (`1a86:55d3`); its serial is recorded on the bench, not here. Address boards
  by `/dev/serial/by-id/`, never by `ttyACMn`; the numbers move and they are
  not identity. Opening an ESP32 serial port can assert DTR/RTS and reset the
  board.
- Never push to `main`. Never edit `STATUS.md` or `TASKS.md`.
- Public repository. **The owner's coordinates are their home address** — a
  mean position never goes in the repository, whatever was printed in chat.
  Per-unit identity that is bench-only: CHIPID, `UBX-SEC-UNIQID`, GNSS module
  serial suffixes, the MeshCore node's **BLE** address, and the other agent's
  dev-board serial. The rule is narrower than an earlier draft of this document
  claimed: the boards' **USB** serials are already published, deliberately, in
  `docs/research/BENCH_DEVICES.md`, which is where board identity lives.

---

## 7. What is on the bench, and what may never be committed

`~/attadipa-bench/` holds the evidence behind most of `VERIFIED_FACTS.md`.
**None of it is committable**: both QMI8658 Rev A datasheets, both AXP2101
datasheets, the LilyGO T-Watch GPS schematic and its renders, the MIA-M10Q data
sheet and integration manual, the factory flash dumps and backups, the upstream
`MyMesh.cpp` and `fnirsi_logger.py` copies, the boot capture, and every bench
GNSS / PMU / power-meter log including `twatch_taper_20260908.csv` and the
`fnirsi-*.csv` captures.

Hardware present and working: the T-Watch S3 Plus, the Waveshare AMOLED 2.06,
an FNIRSI **FNB-58** USB power meter (HID, not serial), GT-U12 and AN3126 GNSS
modules, magnetometers, vibromotors, and a MeshCore node that is already
advertising. **Board identity is not repeated here**: `BENCH_DEVICES.md`
already carries the USB serials and is the one home for them, and what stays
on the bench is listed in §6. The watch is **unprovisioned** — that, not
missing code, is the gap between here and the end goal.

Two measured results worth keeping in your head. **Both are USB *input* power
at the meter, not board consumption**, and neither is quotable without the
state it was taken in.

The Waveshare figure is **413 mW, the median** — not the 415 mW mean over the
same 21 440 samples — with **the cell disconnected**, screen on at minimum
brightness, static on the provisioning entry screen, idle and unprovisioned.
The **bimodal** 84 mA / 213 mA input current belongs to a **different run**,
on 2026-09-05, with **the cell still attached**; whether its 213 mA mode is
charge current is *consistent with* the cell being attached and **is not
established** — nothing measured the charger. Do not carry that bimodality
back onto the 413 mW state, where it was not observed.

The T-Watch reads **779 mW**, and **how much of that is charging the cell
rather than running the watch is `UNKNOWN`** — that conflation is the whole of
open issue #492. The entry's own instruction, verbatim: **do not derive a
battery life, a per-rail split, or a sleep figure from this.**

One number this document used to carry as a rule is not one: 0.121 % of the
**S17** capture was excluded by the filter **that entry** calls a heuristic.
That is one capture's figure under one filter, not a law about the meter. Apply
the same filter to S16 and it deletes that entry's own **1282 mA maximum** — a
real reading, thrown away by a rule imported from another capture. Nothing
licenses filtering 0.12 % of the next one.

---

## 8. How to reach the others

- **#488** is the standing coordination thread with you. I have been posting
  notices there — the lease-admission mechanism, the WIP limit, and #502's
  findings with their measurements.
- Issue comments do **not** start an agent; only the intake gate runs. Annotate
  freely.
- Do **not** export `ATTADIPA_WIP_LIMIT`.

Everything I know is in this document, in the issues, and in the pull request
comments. Nothing is being held back for a later message.
