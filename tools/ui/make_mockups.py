#!/usr/bin/env python3
"""Render the provisioning screen at both panel sizes, before any LVGL is written.

    python3 tools/font/fetch_ttf.py --out artifacts/ui/NunitoSans.ttf
    python3 tools/ui/make_mockups.py --font artifacts/ui/NunitoSans.ttf \
        --out artifacts/ui/mockups

A layout argument is cheap on a picture and expensive in C++. This draws the
screen in HTML at the panel's real pixel size, in the device's own typeface, and
screenshots it -- so a spacing question is answered by looking rather than by
building the firmware and flashing it.

Three things make the output evidence rather than decoration, and all three are
easy to lose in a rewrite:

* **Native size, never scaled.** 410x502 and 240x240 are what the panels are.
  A mockup drawn at 800 px and shrunk hides exactly the failure that matters --
  a touch target too small, a Russian label that does not fit.
* **The device's font.** Nunito Sans at the same sizes the firmware ships.
  A system sans has different metrics and will fit text the panel will not.
* **The tokens, not eyeballed colour.** Every value below is the night column of
  `ui/src/color.cpp:14` -- "constexpr Rgb kWarmIvory{0xFF, 0xF6, 0xE8};" -- and
  every pair carrying a WORD was checked against the measured table in
  `docs/ui/DESIGN_SYSTEM.md:225` -- "**proposed** and none has been checked at
  240 x 240." That table is why an accepted verdict here is a green ring and
  never a green word: `success` on a night surface measures 3.54:1, under the
  4.5:1 that body text needs, and comfortably over the 3.0:1 a graphic does.
  Night has no raised layer at all -- only the day table has that column.

WHAT THESE PICTURES CHANGE FROM TODAY, SAID OUT LOUD. They are proposals, and a
proposal that quietly differs from the shipping screen is worse than no picture:

* **The keys are drawn on `BackgroundSurface`.** The face draws them on
  `BackgroundRaised`, which has no night value, so `color()` returns nullopt and
  each face's `resolved()` helper turns it into black -- today's night keypad is
  thirteen black rectangles on the page. Moving the key surface is a design
  change and #469 decides it; until then these renders show the proposal, not
  the panel.
* **The keypad is one row shorter than the face's**, because these add a step
  pip row above the title and reserve a verdict line. Both are proposals too.
  Every render prints the key size it actually drew, so a number quoted from
  these pictures cannot drift from them -- but it is this script's number and
  not the firmware's, and the two are different geometries. The shipping figure
  is whatever `provision_face.cpp`'s `key_height` computes for the board.

The screens and both keypad arrangements are the ones #469 proposes. When that
issue closes this script stays, because the next screen gets the same treatment.
"""
import argparse
import pathlib
import subprocess
import sys

# ui/src/color.cpp:14-24, the night column of kTable.
PAGE, SURFACE = "#2F3A2E", "#3C4033"
INK, MUTED, ACCENT = "#FFF6E8", "#A7B49C", "#FFC857"
WARNING, SUCCESS = "#FF8A40", "#6FA07A"

FONT = None  # set from --font in main(); the HTML embeds it by absolute path.

BOARDS = {
    "wa": dict(w=410, h=502, margin=24, gap=8, pip=7,
               f_title=20, f_value=28, f_hint=20, f_key=28, f_word=20,
               hint_lines=2, radius=12),
    "tw": dict(w=240, h=240, margin=11, gap=6, pip=5,
               f_title=16, f_value=20, f_hint=16, f_key=20, f_word=13,
               hint_lines=2, radius=8),
}

# Row-major. `None` is an empty cell. Option A keeps today's 3x5; option B is
# the 4x4 that DESIGN_SYSTEM's touch token can nearly be met with on the big
# panel -- the digit block keeps its phone shape in the first three columns and
# the modifiers form a right-hand column.
GRID_A = (3, [["1","2","3"],["4","5","6"],["7","8","9"],
              ["±","0","ERASE"],["CANCEL","OK","OK"]])
GRID_B = (4, [["1","2","3","ERASE"],["4","5","6","±"],
              ["7","8","9","0"],["CANCEL","OK","OK","OK"]])


def key_size(board, grid):
    """The key box this script draws, in panel pixels. Shared with `html()` so
    the number printed for a render is the number that render used."""
    b = BOARDS[board]
    m, gap, W, H = b["margin"], b["gap"], b["w"], b["h"]
    lh_title = round(b["f_title"] * 1.2)
    lh_value = round(b["f_value"] * 1.15)
    lh_hint = round(b["f_hint"] * 1.2)
    y_pips = m
    h_pips = max(3, b["pip"] // 2)
    y_title = y_pips + h_pips + gap // 2
    y_value = y_title + lh_title
    y_msg = y_value + lh_value + gap // 2
    y_pad = y_msg + lh_hint * b["hint_lines"] + gap
    cols, rows = grid
    n_rows = len(rows)
    return ((W - m * 2 - gap * (cols - 1)) // cols,
            (H - y_pad - m - gap * (n_rows - 1)) // n_rows)


def html(board, state, grid):
    b = BOARDS[board]
    m, gap, W, H = b["margin"], b["gap"], b["w"], b["h"]
    usable = W - m * 2

    lh_title = round(b["f_title"] * 1.2)
    lh_value = round(b["f_value"] * 1.15)
    lh_hint = round(b["f_hint"] * 1.2)

    y_pips = m
    h_pips = max(3, b["pip"] // 2)
    y_title = y_pips + h_pips + gap // 2
    y_value = y_title + lh_title
    y_msg = y_value + lh_value + gap // 2
    msg_h = lh_hint * b["hint_lines"]
    y_pad = y_msg + msg_h + gap

    kw, kh = key_size(board, grid)
    cols, rows = grid
    n_rows = len(rows)

    parts = []
    add = parts.append

    if state.get("done"):
        # The frame the finished screen never had: what was set, what was not,
        # and that it leaves on its own.
        ring = b["f_value"] * 2
        # On 240 px the clock and its label do not share a line: "2026-09-07
        # 12:09 UTC-07" wrapped under "Часы" and ran into the row below it.
        # Only the row that needs it. Stacking all three overran 240 px and put
        # the last one through the progress bar; the clock is the one value
        # long enough to need its own line.
        def stacked(value):
            return W < 320 and len(value) > 18
        rows_h = sum(lh_hint * (2 if stacked(v) else 1) for _, v, _ in
                     state["summary"]) + gap * (len(state["summary"]) - 1)
        block = ring + gap * 2 + lh_value + gap * 2 + rows_h
        # The returning bar is not empty space to centre through: on 240 px the
        # summary ran straight over it.
        tail = 3 + gap + lh_hint + gap
        top = (H - m * 2 - tail - block) // 2 + m
        add(f'<div style="position:absolute;left:{(W - ring) // 2}px;top:{top}px;'
            f'width:{ring}px;height:{ring}px;border-radius:{ring // 2}px;'
            f'border:{max(2, gap // 2)}px solid {SUCCESS}"></div>')
        add(f'<div class="ctr" style="top:{top}px;height:{ring}px;'
            f'font-size:{ring // 2}px;color:{SUCCESS};line-height:{ring}px">&#10003;</div>')
        yy = top + ring + gap * 2
        add(f'<div class="ctr" style="top:{yy}px;height:{lh_value}px;'
            f'font-size:{b["f_value"]}px;color:{ACCENT}">{state["title"]}</div>')
        yy += lh_value + gap * 2
        for label, value, tone in state["summary"]:
            stack = stacked(value)
            if stack:
                add(f'<div style="position:absolute;left:{m}px;top:{yy}px;'
                    f'width:{usable}px;font-size:{b["f_hint"] - 3}px;color:{MUTED};'
                    f'letter-spacing:.5px;text-transform:uppercase;opacity:.8;'
                    f'line-height:{lh_hint}px">{label}</div>')
                add(f'<div style="position:absolute;left:{m}px;top:{yy + lh_hint}px;'
                    f'width:{usable}px;font-size:{b["f_hint"]}px;color:{tone};'
                    f'line-height:{lh_hint}px">{value}</div>')
            else:
                add(f'<div class="row" style="top:{yy}px;height:{lh_hint}px">'
                    f'<span style="font-size:{b["f_hint"]}px;color:{MUTED}">{label}</span>'
                    f'<span style="font-size:{b["f_hint"]}px;color:{tone}">{value}</span>'
                    f'</div>')
            yy += lh_hint * (2 if stack else 1) + gap
        # The three ticks before it leaves, drawn rather than described:
        # `firmware/main/waveshare_board.cpp` kDoneTicks is a real wait and the
        # screen never said so.
        bar_y = H - m - lh_hint - gap * 2
        add(f'<div style="position:absolute;left:{m}px;top:{bar_y}px;'
            f'width:{usable}px;height:3px;border-radius:2px;background:{MUTED};'
            f'opacity:.25"></div>')
        add(f'<div style="position:absolute;left:{m}px;top:{bar_y}px;'
            f'width:{usable * 2 // 3}px;height:3px;border-radius:2px;'
            f'background:{MUTED};opacity:.7"></div>')
        add(f'<div class="ctr" style="top:{bar_y + gap + 3}px;height:{lh_hint}px;'
            f'font-size:{b["f_hint"]}px;color:{MUTED};opacity:.75">'
            f'{state["footer"]}</div>')
        body = "".join(parts)
    else:
        # Title, with the step as pips on the same line so it costs no height.
        add(f'<div class="ctr" style="top:{y_title}px;height:{lh_title}px;'
            f'font-size:{b["f_title"]}px;color:{ACCENT}">{state["title"]}</div>')
        n, of = state["step"]
        pw, pgap = b["pip"] * 4, b["pip"]
        total = of * pw + (of - 1) * pgap
        x = (W - total) // 2
        for i in range(of):
            on = i < n
            add(f'<div style="position:absolute;left:{x + i * (pw + pgap)}px;'
                f'top:{y_pips}px;width:{pw}px;height:{h_pips}px;'
                f'border-radius:{h_pips // 2}px;background:{ACCENT if on else MUTED};'
                f'opacity:{1 if on else .3}"></div>')

        # The value, with the typed part in ink and the slots still waiting in
        # muted -- so the eye sees how much is left without counting.
        typed, slots = state["value"]
        mark = state.get("mark")
        add(f'<div class="ctr" style="top:{y_value}px;height:{lh_value}px;'
            f'font-size:{b["f_value"]}px;letter-spacing:{max(2, gap // 2)}px">'
            f'<span style="color:{WARNING if mark else INK};'
            f'{"text-decoration:underline;text-underline-offset:4px;text-decoration-thickness:2px" if mark else ""}">'
            f'{typed}</span>'
            f'<span style="color:{MUTED};opacity:.55">{slots}</span></div>')

        # The message slot. One row, two jobs, and now they do not look alike:
        # an instruction is muted text; a refusal is `warning`, which §3.2
        # measures at 5.08:1 on the night page -- the lowest of the four accents
        # that clear 4.5:1 there, and so a safe floor for a word rather than
        # only a graphic.
        v = state.get("verdict")
        if v:
            text, tone = v
            add(f'<div class="ctr wrap" style="top:{y_msg}px;height:{msg_h}px;'
                f'font-size:{b["f_hint"]}px;color:{tone};font-weight:600">'
                f'{text}</div>')
        else:
            add(f'<div class="ctr wrap" style="top:{y_msg}px;height:{msg_h}px;'
                f'font-size:{b["f_hint"]}px;color:{MUTED}">{state["hint"]}</div>')

        seen = set()
        for r, row in enumerate(rows):
            for c, key in enumerate(row):
                if key is None or (r, key) in seen:
                    continue
                span = row.count(key) if row.count(key) > 1 else 1
                if span > 1:
                    seen.add((r, key))
                    if c != row.index(key):
                        continue
                    c = row.index(key)
                w = kw * span + gap * (span - 1)
                x = m + c * (kw + gap)
                y = y_pad + r * (kh + gap)
                label = {"ERASE": state["erase"], "CANCEL": state["cancel"],
                         "OK": state["ok"]}.get(key, key)
                word = key in ("ERASE", "CANCEL", "OK")
                is_ok = key == "OK"
                bg = ACCENT if is_ok else SURFACE
                fg = PAGE if is_ok else INK
                fs = b["f_word"] if word else b["f_key"]
                add(f'<div class="key" style="left:{x}px;top:{y}px;width:{w}px;'
                    f'height:{kh}px;background:{bg};color:{fg};font-size:{fs}px;'
                    f'border-radius:{b["radius"]}px">{label}</div>')
        body = "".join(parts)

    return f"""<meta charset="utf-8"><style>
@font-face {{ font-family:N; src:url("file://{FONT}"); }}
* {{ margin:0; box-sizing:border-box; }}
body {{ width:{W}px; height:{H}px; background:{PAGE}; font-family:N,sans-serif;
        overflow:hidden; position:relative; -webkit-font-smoothing:antialiased; }}
.ctr {{ position:absolute; left:0; width:{W}px; text-align:center;
        display:flex; align-items:center; justify-content:center; }}
.wrap {{ padding:0 {m}px; line-height:{lh_hint}px; display:block;
         text-align:center; overflow:hidden; }}
.row {{ position:absolute; left:{m}px; width:{usable}px; display:flex;
        align-items:center; justify-content:space-between; }}
.key {{ position:absolute; display:flex; align-items:center;
        justify-content:center; }}
</style>{body}"""


STATES = {
  "1-date": dict(title="Дата", step=(1, 4), value=("2026", "-__-__"),
      hint="год, месяц, день", erase="Стереть", cancel="Отмена", ok="OK"),
  "2-offset": dict(title="Местный сдвиг", step=(3, 4), value=("-07", ":__"),
      hint="местное время минус UTC; ± меняет знак",
      erase="Стереть", cancel="Отмена", ok="OK"),
  "3-rejected": dict(title="Время, UTC", step=(2, 4), value=("27:09", ""),
      mark=True, hint="часы и минуты по UTC",
      verdict=("Не принято: часов больше 23", WARNING),
      erase="Стереть", cancel="Отмена", ok="OK"),
  "4-passkey": dict(title="Код узла", step=(4, 4), value=("", "______"),
      hint="шесть цифр с узла; OK без цифр — пропустить",
      erase="Стереть", cancel="Отмена", ok="OK"),
  "5-done": dict(done=True, title="Готово", summary=[
      ("Часы", "2026-09-07  12:09  UTC-07", INK),
      ("Узел", "не привязан", MUTED),
      ("Код", "пропущен", WARNING)],
      footer="возврат к циферблату…"),
}


def main() -> int:
    global FONT
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", required=True, type=pathlib.Path,
                    help="Nunito Sans TTF; tools/font/fetch_ttf.py writes one")
    ap.add_argument("--out", required=True, type=pathlib.Path,
                    help="directory for the HTML and PNG pairs")
    ap.add_argument("--chromium", default="chromium",
                    help="the headless browser to screenshot with")
    args = ap.parse_args()

    if not args.font.is_file():
        print(f"no font at {args.font}; run tools/font/fetch_ttf.py first",
              file=sys.stderr)
        return 2
    FONT = args.font.resolve()
    args.out.mkdir(parents=True, exist_ok=True)

    made = []
    for board in ("wa", "tw"):
        for grid_name, grid in (("A", GRID_A), ("B", GRID_B)):
            for sname, st in STATES.items():
                # The summary screen has no keypad, so it is the same picture
                # under either arrangement and is drawn once.
                if st.get("done") and grid_name == "B":
                    continue
                f = args.out / f"{board}-{grid_name}-{sname}.html"
                f.write_text(html(board, st, grid), encoding="utf-8")
                png = f.with_suffix(".png")
                subprocess.run([args.chromium, "--headless", "--no-sandbox",
                                "--disable-gpu", "--hide-scrollbars",
                                f"--screenshot={png}",
                                f"--window-size={BOARDS[board]['w']},{BOARDS[board]['h']}",
                                "--default-background-color=00000000",
                                str(f)], check=True, capture_output=True)
                made.append(png)
                b = BOARDS[board]
                if st.get("done"):
                    # The finished screen draws no keypad. Printing a key size
                    # for it published a measurement of something not on the
                    # page.
                    print(f"{png.name}: no keypad on {b['w']}x{b['h']}",
                          file=sys.stderr)
                else:
                    kw, kh = key_size(board, grid)
                    print(f"{png.name}: keys {kw}x{kh} px on "
                          f"{b['w']}x{b['h']}", file=sys.stderr)

    for p in made:
        print(p)
    print(f"\n{len(made)} renders. Open them -- the point of this script is "
          f"the looking, not the exit code.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
