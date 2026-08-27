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
- `firmware/sdkconfig` is generated and gitignored, and `idf.py` reuses whatever
  is already there in preference to `sdkconfig.defaults`. A stale one is a
  silent failure, not a build error: a leftover `# CONFIG_BT_ENABLED is not set`
  compiled the whole NimBLE path out of the image, the build exited 0 with no
  warning, and a Bluetooth-less binary went to the bench. Delete it before you
  trust a build whose behaviour depends on a Kconfig option, and check the
  image size actually moved. `sdkconfig.ramprobe`'s header documents the same
  trap from the other direction: `idf.py` writes `sdkconfig` into the *project*
  directory, so a second build directory silently reuses the first one's
  configuration.
- UI or input changes also require the repository's `watch-ui-testing` skill;
  compilation alone is not screen evidence.

Relevant facts live under `docs/research/`; durable architecture belongs in an
ADR. Test and build procedures live beside the code they exercise.
