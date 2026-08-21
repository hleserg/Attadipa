# Reconciliation against the final master prompt — 2026-08-21

[`docs/master-prompt-final.md`](../master-prompt-final.md) §75 lists eight
mandatory P0 corrections and instructs, in §75.3, to *"re-check whether each
review issue below is still present"* rather than blindly applying a stale
patch. This file is that re-check, and then the record of what closing each one
actually changed.

It is not a to-do list — [TASKS.md](../../TASKS.md) is. It is the evidence that
each item was found, was real, and was closed, kept together in one place so
that the next reader does not have to reconstruct it from eight commit
messages.

## Verdict on arrival

All eight were still present. Two were worse than the review described, and one
was narrower.

| | Item | Still present? | What was actually found |
|---|---|---|---|
| **A** | Split hardware inventory from product capabilities | **yes** | Worse than described. There was not a *mixed* model — there was only *one* layer, the hardware one, and applications were pointed straight at it. `ARCHITECTURE.md` §3 lists `Display · Touch · … · Lora · Gnss · …` as the app-facing set. No product capability such as `Position` or `MeshMessaging` existed anywhere in the repository. |
| **B** | Remove ambiguous `has()` | **yes** | `ARCHITECTURE.md:139`, `ADR-0001:77,138`, `README.md:94`, `TASKS.md:512`, `ADR-0004:239`, `HARDWARE_MATRIX.md:356`, `VERIFIED_FACTS.md:336`. Documented as *"cheap, for gating UI"* — the exact use final §7 shows to be ambiguous. |
| **C** | Radio vs LoRa | **yes** | `ADR-0001:51` — *"one of five **LoRa** chips — SX1262, SX1280, CC1101, LR1121, SI4432"*. Repeated in `ARCHITECTURE.md:113`, `CLAUDE.md:26`, `HARDWARE_MATRIX.md`, `VERIFIED_FACTS.md:102`. Two of the five have no LoRa modulator. |
| **D** | Local T-Watch mesh vs node-only | **yes** | The contradiction was sitting in the ADR *index*: `docs/adr/README.md` said *"the watch does not run mesh, because the radio is in the node"*, while `ARCHITECTURE.md` mapped a local radio to a mesh service. `ADR-0005` built a whole protocol on the former. |
| **E** | Heading reference frames | **yes**, narrower than described | Not a wrong model — **no** model. Heading appears as prose in seven documents and as a structure in none. No `reference_frame`, no `source`, no `confidence`. The specific error final §10 warns about (node magnetometer read as watch heading) had not been made yet, because there was nothing to make it with. |
| **F** | EN/RU localization | **yes**, total | Zero occurrences in the repository. Not deferred, not backlogged — absent. The only mentions of Russian were "the specification documents are in Russian". |
| **G** | TASKS/STATUS/DEPENDENCIES consistent | **yes** | See below. |
| **H** | ADR statuses mean something | **yes** | All five ADRs were `proposed`, while `ARCHITECTURE.md` presented their contents as settled fact and 32 tasks depended on them. Final §74: *"Do not build dozens of layers on an allegedly provisional decision while treating it as immutable."* |

One further item, not on the §75 list, was found during the same pass and is
treated as P0 because the final prompt names it directly:

| | Item | Where |
|---|---|---|
| **I** | *"Ownership means initialises it"* is too strong | Final §32 says so almost verbatim: *"A previous architecture definition equated ownership with 'initializes the part'. That is too strong."* `ARCHITECTURE.md` §4 defines ownership exactly that way, and four ownership tables are built on it. |

## What closed each one

Filled in as each lands, so that a half-finished reconciliation is visible as
one rather than reading as a plan.

| | Closed by | Status |
|---|---|---|
| **A** | ADR-0007 · `ARCHITECTURE.md` §3 | pending |
| **B** | ADR-0007 | pending |
| **C** | ADR-0003 · `RADIO_MATRIX.md` | pending |
| **D** | ADR-0008 · notice on ADR-0005 · `adr/README.md` | pending |
| **E** | ADR-0009 | pending |
| **F** | ADR-0010 · `DESIGN_SYSTEM.md` | pending |
| **G** | every commit in this pass | pending |
| **H** | `adr/README.md` index | pending |
| **I** | `ARCHITECTURE.md` §4 | pending |

## G in detail — what was actually stale

Final §73 says task and status consistency is a deliverable and must be updated
*in the same logical commit* as the change it describes. The honest finding is
that this repository's `TASKS.md` and `STATUS.md` were not several commits
behind — they were rewritten on each of the last four commits and were accurate
at each one. The staleness the review found was real earlier in the day and has
a different cause worth naming: **status was being rewritten as a batch at the
end of a work session rather than as part of the change.** That works until a
session ends early, and then it does not.

What is genuinely inconsistent right now, and is fixed in this pass:

- `TASKS.md` T-013 is described as *"blocked on reading MeshCore"*. MeshCore has
  been read. The task is not blocked; it is the ADR-0003 that item **C** needs.
- `STATUS.md` names two long-running recon operations (`recon:power-rails`,
  `recon:gnss-heading`) as running. Both terminated on a spend limit and
  returned nothing. A status file that lists a dead process as in-flight is the
  precise failure §73 describes.
- `docs/research/DEPENDENCIES.md` pins ESP-IDF but not LVGL, and final §76–§77
  and §51 make the LVGL pin a prerequisite for the M1 font and image pipelines.
  It is the one dependency decision that blocks Step 8, and it is recorded as
  unresolved rather than left implicit.
- No task existed for localization, for the asset pipeline, or for the design
  system. Final §87 M0.5 requires *"asset and localization work are in
  backlog"* — as real entries, not as prose.

## What the review did not invalidate

Worth stating, because a review of this size invites over-correction:

- **Every hardware fact in [VERIFIED_FACTS](VERIFIED_FACTS.md) stands.** The
  corrections are to the *model*, not the measurements. The schematics say what
  they said this morning.
- **[OD-1](OWNER_DECISIONS.md) stands**, and final §3 and §9 restate it almost
  word for word.
- **The seven-state availability enum stands.** Final §8 endorses it by name and
  adds the requirement that the transition model be centralized and tested —
  which [ADR-0004](../adr/0004-capability-sources.md) §2a already contains.
- **The two-ages rule stands** — final §8 states it independently.
- **ADR-0001's rejected alternatives stand**, all four, for the reasons given.
  Item **A** replaces what ADR-0001 decided, not what it ruled out.
