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
- **The token that cannot be met is a documented price, not a silent one --
  and the price is usually the layout, not the token.** The entry screen used
  to cap its key at the touch minimum and land under it, because fourteen keys
  do not fit a 240 px panel at any cell size. Six do, at the full target:
  `ui/lvgl/provision_face.cpp:121` — "  // Six keys, not fourteen, so a key can be a full touch target on both".
  Name the constraint that won, in the file that loses -- and check first
  whether the count is what has to lose.
- **An undefined colour resolves to black, in four separate copies.**
  `ui/src/color.cpp:113` — "    return e.kind == ColorKind::Foreground ? e.day : std::nullopt;" — returns
  no night value for a background on purpose, and most faces' local `resolved()`
  helper turns that `nullopt` into black: `ui/lvgl/mesh_face.cpp:15` —
  "  return value ? lv_color_hex(value->packed()) : lv_color_black();". So a role you forget to define does not fail
  the build, it paints a black rectangle. The entry screen is the one that
  does not: its helper takes the fallback as an argument, so every caller has
  to say what an undefined role becomes there
  (`ui/lvgl/provision_face.cpp:11` — "lv_color_t resolved(ColorRole role, Theme theme, PixelCost pixel_cost,").
  Night has no `BackgroundRaised`, so a key that fell through to black would
  have been a hole; it falls through to the surface instead. `ColorRole::Danger` is undefined in
  every column — `ui/src/color.cpp:67` — "{ColorRole::Danger, ColorKind::Foreground, std::nullopt," —
  because this palette has no red. Use `Warning` for a refusal.
- **Contrast decides whether a state may be a word.** The night table measures
  `success` at 3.54:1 on a surface — enough for a graphic, under the
  4.5:1 a word needs. (Night has no raised layer at all —
  `docs/ui/DESIGN_SYSTEM.md:143` — "**Night** — there is no raised layer; §3.1 records that gap." —
  so a night ratio is always a surface or page one.) An accepted state is a ring or a check, not green text.
  And the spacing family is itself unverified at the small size:
  `docs/ui/DESIGN_SYSTEM.md:240` — "**proposed** and none has been checked at 240 × 240."
- **LVGL's three label traps, all three already paid for.**
  `LV_LABEL_LONG_DOT` ellipsises only where the height is FIXED — with the
  height left at content the label grows downward through whatever is beneath
  it: `ui/lvgl/mesh_face.cpp:319` — "    // `LV_LABEL_LONG_DOT` puts the dots in only where the height is fixed;".
  **A label created bare is that same trap with nothing to read.** There is no
  long-mode call to find, and the default is content height with
  `LV_LABEL_LONG_WRAP`, so one line break in text that arrived off the link
  grows the row — a short name, not a long one:
  `ui/lvgl/mesh_face.cpp:277` — "    // content height and `LV_LABEL_LONG_WRAP`, so a name carrying a line break".
  This is the one that was missing here, and #475 paid for it a third time.
  `LV_LABEL_LONG_CLIP` on a centred label clips *both* ends. The entry screen
  used to lose a hint's first word as well as its last that way and now wraps
  every line instead; the clock's date still clips, on a line short enough that
  it does not: `ui/lvgl/clock_face.cpp:125` — "  lv_label_set_long_mode(date_, LV_LABEL_LONG_CLIP);".
  A centred CLIP is a promise the text will fit, not a way of handling text
  that does not.
- **`tools/ui/check_raw_values.py` does not see every literal.** It cannot read
  inside a ternary, and it cannot follow a local wrapper — a hard-coded pixel
  in either is invisible to it and the check stays green. Its silence is not
  evidence; read the diff.
- **Rendered tests are rare, so add one rather than assume it exists.**
  `tests/CMakeLists.txt:367` — "    add_executable(test_mesh_face test_mesh_face.cpp)" — is the only face that
  renders into memory and counts pixels. Clock, nav and provisioning have no
  such test. These live in a simulator build (`-DATTADIPA_BUILD_SIMULATOR=ON`),
  because LVGL is only configured there.
- **A pixel test needs a counter-check.** Comparing two frames proves nothing
  when the two frames are identical, and a value on a symmetry point hides the
  bug it was written for — 180° hides a sign flip, 0° and 90° hide a sin/cos
  swap. Assert that the thing you changed *did* change, then mutate the source
  and watch the test fail before you believe it.

Screens belong to applications, not to boards: `ui/` asks what a device can do
and never which board it is. That one is an architecture invariant and is not
open to redesign.

**Everything else here is a finding, not a fence.** The traps above are things
that already cost a round — they are worth knowing before you touch a label, and
none of them says a screen must stay as it is. `docs/ui/DESIGN_SYSTEM.md` is a
document this directory owns: the measured contrast ratios are evidence and hold
until re-measured. **The palette is not this directory's to revise**: the
owner answered it on issue #57 and it lives where an owner-only decision lives —
`docs/research/OWNER_DECISIONS.md:1008` — "## OD-15 — A7 and A8: the canonical palette wins" — so a
change to a colour value is proposed there, not made here, and
`tests/test_ui_tokens.cpp:192` — "    CHECK(undefined_in_day == 1);" holds its
shape meanwhile. The type scale, the spacing family and the component shapes
**are** design, and the UI agent revises them the same way it revises a screen.
Record what changed and why, so the next reader knows which numbers were
measured and which were chosen. A screen's behaviour is decided in its issue.
