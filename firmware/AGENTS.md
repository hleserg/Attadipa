# Firmware and board work

- Read the evidence for every pin, rail, device, bus and board revision you
  touch. Presence on a schematic does not prove that a vendor BSP drives it.
- Keep board differences in the BSP/platform boundary. Applications consume
  capabilities and must not learn GPIOs, buses, rails or board identities.
- Give every fitted part an owner and state even when no application uses it.
- Prefer an early physical probe over architecture built around an assumed
  value. Preserve calibration where real hardware can vary.
- Build every affected target and run relevant host tests. Report physical
  behaviour as `NOT EXECUTED — HARDWARE REQUIRED` until it ran on the board.
- UI or input changes also require the repository's `watch-ui-testing` skill;
  compilation alone is not screen evidence.

Relevant facts live under `docs/research/`; durable architecture belongs in an
ADR. Test and build procedures live beside the code they exercise.
