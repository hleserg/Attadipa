# Research and hardware evidence

- Trace claims to primary sources: the exact datasheet, board-revision
  schematic, vendor source, upstream implementation, or reproducible bench log.
- Preserve provenance and distinguish component capability, board wiring,
  vendor-driver support and behaviour actually measured on Attadipa hardware.
- Write `UNKNOWN` when evidence is insufficient. State what was checked, why it
  did not establish the fact, the impact, and the smallest next action.
- Research output is evidence, not production implementation. Link an existing
  fact instead of copying it into a new ledger.
- Never promote a mock, simulator result or estimate to hardware `PASS` or
  `MEASURED`.

Use `VERIFIED_FACTS.md` as the fact index and a focused report for supporting
detail. Architecture decisions belong in `docs/adr/`; task status stays in the
GitHub issue or pull request.
