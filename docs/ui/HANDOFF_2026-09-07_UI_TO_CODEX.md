# UI and UX hand over to a second agent — 7 September 2026

From this date UI and UX work is Codex's; hardware, firmware, OS, GNSS, radio
and power stay with the Claude agent. This file is the state of the transfer,
written once. It is not a ledger — when a fact here stops being true, the issue
or `ui/AGENTS.md` is where it gets corrected, not this document.

## What each agent owns

| | |
| --- | --- |
| **UI agent** | `ui/`, `sim/*_screen.cpp`, `docs/ui/`, `tools/ui/`, `tests/test_*_face.cpp`, and `l10n/` when the change is wording |
| **Not the UI agent** | `firmware/`, `platform/`, `core/`, `link/`, `gnss/`, `docs/research/`, `.github/` |

At the seam — a face's interface changes and a board file must follow — the
change is **one** pull request, agreed in the issue before either agent starts.
Splitting it leaves `main` broken between two merges.

`ui/AGENTS.md` is the durable half of this handover and is loaded by reading it;
this file is the perishable half. The root `AGENTS.md` names both scoped files
under **Scoped instructions**, which is the only reason an agent finds them:
verified on this date with `codex debug prompt-input "hello"`, which assembles
the root file and nothing nested. The `watch-ui-testing` skill is already
reachable through `.codex/skills/watch-ui-testing`.

## What is waiting

**[#469](https://github.com/hleserg/Attadipa/issues/469) — the provisioning
screen.** Six findings, every one driven through the real `ProvisioningEntry`
over the debug socket rather than read off the source. Three verdict states —
`Pending`, `Forgotten`, `Accepted` — were **not reachable** through the
simulator's provisioner stub and are marked NOT VERIFIED in the issue; they need
either a better stub or the physical node.

**One owner decision is open and must not be assumed.** The keypad is 14 keys.
Today's 3 × 5 gives 63 px keys on the Waveshare against a 87 px token; a 4 × 4
gives 81 px. On the T-Watch neither reaches the 61 px token — 26 px and 35 px —
so the small panel is a choice between two undersized grids, not a fix. Mockups
of both exist for every screen; the owner has **not** chosen. Ask, do not infer
a decision from the fact that the pictures were drawn.

Render them with:

```
python3 tools/font/fetch_ttf.py --out artifacts/ui/NunitoSans.ttf
python3 tools/ui/make_mockups.py --font artifacts/ui/NunitoSans.ttf --out artifacts/ui/mockups
```

## What is already established, so it is not re-litigated

- **Mesh is done.** [#465](https://github.com/hleserg/Attadipa/issues/465)
  closed with [#466](https://github.com/hleserg/Attadipa/pull/466), merged as
  `53f59261`. The message label's one-line-plus-ellipsis behaviour now has a
  rendered regression test on both panels; it was written for that merge and
  lands with this one.
- **The clock and navigation faces are designed and the navigation one was
  accepted by the owner.** Provisioning is the last screen still in its
  prototype shape.
- **Red does not exist in this palette** and that is an identity decision, not
  an omission. A refusal is `Warning` amber.
- **The firmware still sets `Locale::En`.** A Russian catalogue and a Russian
  simulator are not evidence of a Russian UX on the device. Do not report a
  Russian screen as shipped until the firmware selects it.
- A screenshot from the simulator is evidence about layout, colour, wrapping,
  contrast and touch geometry. It is **never** evidence about the panel's real
  colour, refresh, sunlight legibility, touch sensitivity or power, and a
  simulator run is never written as a hardware `PASS`.

## Process notes that cost a round each to learn

- **One writer lease covers the whole repository, and two agents now share it.**
  Claim it, push, release immediately — never hold it across CI or review. A
  claim marked `kind=hosted` belongs to a running workflow; do not break it.
- **A merged status is not an approving review.** Read the `ai-review:blocking`
  label, not the merge box.
- **The review caps at five rounds.** Past the cap `ai-review:pass` means the
  review *ended*, not that it passed. Read `round=N` from the ledger comment's
  state block; the comment is edited in place, so counting comments misleads.
- **A pull request conflicting with `main` fires no checks at all.** Zero runs
  looks the same as parked runs and is not.
- **A review run that dies posts no verdict and leaves the check green.**
  `gh run rerun` gets the round back.
- Merge conflicts here are staged **by name**. `git add -A` mid-merge commits
  about 4500 generated files.

## Not handed over

The design work itself is Codex's from here; this document does not decide it.
The hardware queue — [#442](https://github.com/hleserg/Attadipa/issues/442),
[#450](https://github.com/hleserg/Attadipa/issues/450),
[#462](https://github.com/hleserg/Attadipa/issues/462),
[#467](https://github.com/hleserg/Attadipa/issues/467),
[#470](https://github.com/hleserg/Attadipa/issues/470),
[#471](https://github.com/hleserg/Attadipa/issues/471),
[#472](https://github.com/hleserg/Attadipa/issues/472) and
[#304](https://github.com/hleserg/Attadipa/issues/304) — stays with the Claude
agent and needs nothing from the UI agent.
