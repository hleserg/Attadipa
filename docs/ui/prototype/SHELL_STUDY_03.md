# Shell Study 03 — interactive review

Open `shell.html?size=small&locale=ru` from an HTTP server rooted in this
directory. The existing `index.html` and its V1/V2 review storage are unchanged.
This page neither reads nor writes that storage. It reuses the approved bundled
Nunito font and day/night glade artwork; no new asset download is needed.

## What works here

Clock → Applications → Navigation / Settings; Settings → Connection & node /
Display / About. Labelled Back returns one level and Watch face returns home.
Escape goes back only from inside the watch content with existing history; it
leaves outside controls and already-cancelled key events alone. This is a browser
convenience, **not** a physical watch-button contract.
The existing firmware button and long-tap bindings are not changed.

Clock is represented by Home; the other two rows reflect the installed
navigation/settings manifests in #496. An unavailable app stays clickable and
explains its state. Mesh has no manifest in that registry yet: the old Mesh
design remains linked outside this shell instead of becoming a fake installed
row. The owner's configurable 1–4 icon shortcuts are deferred in #488.

All telemetry is a fixed demonstration snapshot. Recent node data uses voltage;
stale, unknown and disconnected states show a dash in the persistent status and
explain the reading in node details. An integrated-source example has only the
watch battery. “Low” explicitly means the **watch's** simulated 8% charge; it
does not infer a node charge threshold from voltage. No universal SOC curve,
live polling, BLE, sending or receiver validity calculation exists here.

Display theme and motion controls affect the actual prototype, but do not
persist across reload. System reduced-motion preference always wins. Decorative
fireflies have no connection/position meaning. The fixed date/time is a sample,
not a clock service. The navigation page deliberately has no selected remote
target, distance or needle.

## Geometry and validation

Independent layout tokens target 240×240 and 410×502, not CSS scaling. Adult
touch minima are 61 and 87 pixels from the design system. The small layout uses
27px status + 24px heading + 122px two-row viewport + 61px Back + 2px gaps +
4px bottom. Lists/details scroll inside the frame; the primary clock does not.
The compact gaps and browser radii/padding are explicit **proposed exceptions**
to the Dp family, with their arithmetic and layout cost in
[Design system §10](../DESIGN_SYSTEM.md#10-shell-study-03-browser-only-geometry-proposal).
Settings shows an aria-hidden scrollability cue only when its rows overflow;
it promises neither more content below nor a physical gesture binding.
Panels and local reading surfaces protect text without blurring the artwork.
This review is adult-only, **not Child Mode acceptance**.

Serve the directory, then run the shipped browser test (Node/Chromium required):

```sh
rtk proxy python3 -m http.server 8481 --directory docs/ui/prototype
```

In another terminal:

```sh
rtk npm exec --yes --package=agent-browser -- agent-browser --session shell-check open 'http://localhost:8481/shell.html'
rtk npm exec --yes --package=agent-browser -- agent-browser --session shell-check eval '(async () => eval(await (await fetch("shell-selftest.js")).text()))()'
```

The test runs actual UI controls across size/locale/theme/state combinations,
checks reachable touch targets, label overflow, connected-glyph contrast,
truthful battery units and accessible state, Settings overflow, scoped Escape,
locale-preserving study links, return paths and unchanged browser storage.
Require an empty `failures` array; the
browser command's exit code alone is not the verdict. Inspect screenshots too.
The original `selftest.js` still checks V2 on a **freshly loaded** `index.html?v=2`.

Only the browser was exercised. This is not an LVGL simulator or firmware
acceptance. Panel appearance, sunlight, power, physical input and actual BLE
telemetry: **NOT EXECUTED — HARDWARE REQUIRED**.
