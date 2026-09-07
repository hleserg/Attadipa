# UI, screens and the design system

Everything under `ui/`, the simulator's screens, `docs/ui/`, and the wording in
`l10n/`. Read `docs/ui/DESIGN_SYSTEM.md` before changing a colour, a size or a
space: it holds the tokens and the measured contrast ratios, and it is the
authority this file points at rather than repeats.

- **A UI change that compiles has not been checked.** Use the repository's
  `watch-ui-testing` skill and open the images. Both geometries every time —
  410 × 502 and 240 × 240 — and both locales: Russian is the longer language
  and 240 px is where it stops fitting.
- **Draw it before you build it.** `tools/ui/make_mockups.py` renders a screen
  at the panel's real size in the device's own font, so a layout argument costs
  a picture instead of a firmware flash. Native size only; a scaled mockup hides
  the small touch target and the label that overflows.
- **Answer size questions with arithmetic, not with an eye.** A proposal that
  sounds roomy can be smaller than the one it replaces: a 14-key grid needs
  `ceil(14/cols)` rows, and on the 240 px panel *no* arrangement reaches the
  `touch.min.adult` token's 61 px. Compute the cell before proposing the grid.
- **The token that cannot be met is a documented price, not a silent one.**
  `ui/lvgl/provision_face.cpp:137` — "      static_cast<int>(m.px(dp_of(TouchTarget::Adult))));" — caps a
  key at the touch minimum and the code says why it lands under it. Do the
  same: name the constraint that won, in the file that loses.
- **An undefined colour resolves to black, in four separate copies.**
  `ui/src/color.cpp:113` — "    return e.kind == ColorKind::Foreground ? e.day : std::nullopt;" — returns
  no night value for a background on purpose, and each face's local `resolved()`
  helper turns that `nullopt` into black: `ui/lvgl/provision_face.cpp:13` —
  "  return value ? lv_color_hex(value->packed()) : lv_color_black();". So a role you forget to define does not fail
  the build, it paints a black rectangle. `ColorRole::Danger` is undefined in
  every column — `ui/src/color.cpp:67` — "{ColorRole::Danger, ColorKind::Foreground, std::nullopt," —
  because this palette has no red. Use `Warning` for a refusal.
- **Contrast decides whether a state may be a word.** The night table measures
  `success` at 3.54:1 on a surface — enough for a graphic, under the
  4.5:1 a word needs. (Night has no raised layer at all —
  `docs/ui/DESIGN_SYSTEM.md:143` — "**Night** — there is no raised layer; §3.1 records that gap." —
  so a night ratio is always a surface or page one.) An accepted state is a ring or a check, not green text.
  And the spacing family is itself unverified at the small size:
  `docs/ui/DESIGN_SYSTEM.md:225` — "**proposed** and none has been checked at 240 × 240."
- **LVGL's two label traps, both already paid for once.**
  `LV_LABEL_LONG_DOT` ellipsises only where the height is FIXED — with the
  height left at content the label grows downward through whatever is beneath
  it: `ui/lvgl/mesh_face.cpp:301` — "    // `LV_LABEL_LONG_DOT` puts the dots in only where the height is fixed;".
  `LV_LABEL_LONG_CLIP` on a centred label clips *both* ends, which is why the
  provisioning hints lose their first word as well as their last:
  `ui/lvgl/provision_face.cpp:121` — "  lv_label_set_long_mode(hint_, large ? LV_LABEL_LONG_WRAP".
- **`tools/ui/check_raw_values.py` does not see every literal.** It cannot read
  inside a ternary, and it cannot follow a local wrapper — a hard-coded pixel
  in either is invisible to it and the check stays green. Its silence is not
  evidence; read the diff.
- **Rendered tests are rare, so add one rather than assume it exists.**
  `tests/CMakeLists.txt:353` — "    add_executable(test_mesh_face test_mesh_face.cpp)" — is the only face that
  renders into memory and counts pixels. Clock, nav and provisioning have no
  such test. These live in a simulator build (`-DATTADIPA_BUILD_SIMULATOR=ON`),
  because LVGL is only configured there.
- **A pixel test needs a counter-check.** Comparing two frames proves nothing
  when the two frames are identical, and a value on a symmetry point hides the
  bug it was written for — 180° hides a sign flip, 0° and 90° hide a sin/cos
  swap. Assert that the thing you changed *did* change, then mutate the source
  and watch the test fail before you believe it.

Screens belong to applications, not to boards: `ui/` asks what a device can do
and never which board it is. Durable direction is in `docs/ui/DESIGN_SYSTEM.md`
and the ADRs; a screen's behaviour is decided in its issue.
