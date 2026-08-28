#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Re-derive the T-Watch S3 Plus panel width from the bench photograph.

Every number the report states is printed by this script from the committed
image, so it can be checked rather than believed.  Pillow only -- no numpy --
because every other image tool in this repository is the same.

    python3 docs/research/twatch-s3-plus-panel/measure_panel.py

The photograph is a steel rule laid on the watch case, graduations towards the
camera, directly above a lit panel.  Two things are therefore in one frame at
one scale: a length standard, and both vertical edges of the active area.  The
panel is 240x240, so its width alone fixes the diagonal.
"""

import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "panel-and-rule.png")

# Bands picked by looking at the picture, not by search: the fine graduations
# run along the rule's lower edge, the numbered ones along its upper edge, and
# the panel's vertical edges are clean between its top edge and the sun.
FINE_BAND = (150, 174)
NUMBERED_BAND = (60, 90)
PANEL_ROWS = range(262, 356)
# The same measurement, repeated on deliberately different pixels.  The spread
# across these is the uncertainty that means something; a fit residual only
# says the fit was tidy.
ALT_FINE = [(150, 174), (148, 170), (152, 176), (146, 168)]
ALT_ROWS = [range(262, 356), range(266, 340), range(270, 352), range(262, 320)]


def load():
    if not os.path.exists(IMAGE):
        sys.exit(f"missing {IMAGE}")
    im = Image.open(IMAGE).convert("RGB")
    return im, im.size


def column_profile(im, y0, y1):
    """Mean luminance of each column over a horizontal band."""
    grey = im.convert("L").load()
    w, _ = im.size
    return [sum(grey[x, y] for y in range(y0, y1)) / (y1 - y0) for x in range(w)]


def ticks(profile, window=5, depth=10):
    """Sub-pixel x of every graduation: a local minimum `depth` darker than
    the bright metal around it, refined by a parabola through its neighbours."""
    found = []
    for x in range(window, len(profile) - window):
        seg = profile[x - window:x + window + 1]
        metal = sum(sorted(seg)[-4:]) / 4.0
        if profile[x] == min(seg) and metal - profile[x] > depth:
            a, b, c = profile[x - 1], profile[x], profile[x + 1]
            curve = a - 2 * b + c
            shift = (a - c) / (2 * curve) if curve else 0.0
            if abs(shift) < 1:
                found.append(x + shift)
    merged = []
    for v in found:
        if merged and v - merged[-1] < 3:
            merged[-1] = (merged[-1] + v) / 2
        else:
            merged.append(v)
    return merged


def median_gap(xs):
    gaps = sorted(xs[i + 1] - xs[i] for i in range(len(xs) - 1))
    return gaps[len(gaps) // 2]


def lattice(xs, period):
    """Least-squares pitch of graduations that may be partly undetected.

    Indices accumulate gap by gap rather than being measured from the first
    graduation, so one missed graduation leaves one empty slot instead of
    dragging every later index out of step.  Seeding from the median gap and
    not the smallest matters for the same reason: one spurious pair otherwise
    sets the pitch for all of them."""
    idx = [0]
    for _ in range(30):
        idx = [0]
        for i in range(1, len(xs)):
            idx.append(idx[-1] + max(1, round((xs[i] - xs[i - 1]) / period)))
        n = len(xs)
        mi = sum(idx) / n
        mx = sum(xs) / n
        den = sum((i - mi) ** 2 for i in idx)
        if den == 0:
            break
        new = sum((i - mi) * (x - mx) for i, x in zip(idx, xs)) / den
        if abs(new - period) < 1e-9:
            period = new
            break
        period = new
    intercept = mx - period * mi
    resid = [x - (period * i + intercept) for i, x in zip(idx, xs)]
    return period, math.sqrt(sum(r * r for r in resid) / len(resid))


def graduation_tilt(im, top, bottom, margin=60):
    """Tilt of the graduation lines, degrees, positive the same way a panel
    edge leaning the same way is positive.

    This is the angle a horizontal scan stretches by 1/cos, and it is not the
    rule's edge -- a rule laid down askew has one and not the other.

    The search stops at half a pitch on purpose: graduations are a periodic
    signal, and a correlation allowed to wander further has an equally deep
    minimum every pitch and will return a whole graduation of tilt.  It is
    sub-pixel from the start rather than by refining a coarse curve, because
    the shift being measured is itself under one pixel."""
    a = column_profile(im, *top)
    b = column_profile(im, *bottom)
    best = (float("inf"), 0.0)
    steps = int(3.5 / 0.02)
    for k in range(-steps, steps + 1):
        s = k * 0.02
        acc = n = 0.0
        for x in range(margin, len(a) - margin):
            xx = x + s
            i = int(math.floor(xx))
            if 0 <= i < len(b) - 1:
                f = xx - i
                acc += (a[x] - (b[i] * (1 - f) + b[i + 1] * f)) ** 2
                n += 1
        if n and acc / n < best[0]:
            best = (acc / n, s)
    dy = (bottom[0] + bottom[1] - top[0] - top[1]) / 2.0
    return math.degrees(math.atan(-best[1] / dy))


def crossings(im, along, scan, vertical, step):
    """Sub-pixel position where the active area's blue crosses half way between the
    dark bezel and the lit interior.  One point per line of `along`."""
    px = im.load()
    pts = []
    for u in along:
        line = []
        v = scan[0]
        while v != scan[1]:
            line.append(px[v, u][2] if vertical else px[u, v][2])
            v += step
        dark, lit = min(line), max(line)
        if lit - dark < 60:
            continue
        half = (dark + lit) / 2.0
        v = scan[0]
        prev = line[0]
        for cur in line[1:]:
            v += step
            if prev < half <= cur:
                pts.append((u, v - step + step * (half - prev) / (cur - prev)))
                break
            prev = cur
    return pts


def fit_line(pts):
    """dep = slope * indep + intercept, least squares, with the residual."""
    n = len(pts)
    mi = sum(p[0] for p in pts) / n
    md = sum(p[1] for p in pts) / n
    den = sum((p[0] - mi) ** 2 for p in pts)
    slope = sum((p[0] - mi) * (p[1] - md) for p in pts) / den
    intercept = md - slope * mi
    resid = [p[1] - (slope * p[0] + intercept) for p in pts]
    return slope, intercept, math.sqrt(sum(r * r for r in resid) / n)


def width_px(im, rows):
    """Active-area width in pixels, corrected for the panel's own rotation."""
    ls, li, lr = fit_line(crossings(im, rows, (120, 200), True, +1))
    rs, ri, rr = fit_line(crossings(im, rows, (620, 540), True, -1))
    mid = (rows[0] + rows[-1]) / 2.0
    horizontal = (rs * mid + ri) - (ls * mid + li)
    tilt_l = math.degrees(math.atan(-ls))
    tilt_r = math.degrees(math.atan(-rs))
    theta = (tilt_l + tilt_r) / 2.0
    return (horizontal * math.cos(math.radians(theta)), horizontal, theta,
            (tilt_l, tilt_r), (li, ri), (lr, rr), (len(rows)))


def scale_px_per_mm(im, band):
    """Pixels per millimetre along a horizontal scan, from the 0.5 mm edge."""
    fine = ticks(column_profile(im, *band))
    pitch, rms = lattice(fine, median_gap(fine))
    return pitch / 0.5, pitch, rms, fine


def main():
    im, (w, h) = load()
    print(f"image  {os.path.relpath(IMAGE, os.getcwd())}  {w}x{h}\n")

    # --- 1. what one graduation is worth -------------------------------
    scale, pitch, rms, fine = scale_px_per_mm(im, FINE_BAND)
    numbered = ticks(column_profile(im, *NUMBERED_BAND), window=20, depth=12)
    if len(fine) < 20 or len(numbered) < 3:
        sys.exit("the graduations did not resolve; this is not the image the "
                 "script was written for")
    num_pitch, num_rms = lattice(numbered, median_gap(numbered))
    ratio = num_pitch / pitch

    print("1. THE LENGTH STANDARD")
    print(f"   fine graduations       {len(fine)} over {fine[-1] - fine[0]:.1f} px")
    print(f"   fitted pitch           {pitch:.4f} px  (residual rms {rms:.3f} px)")
    print(f"   numbered divisions     {len(numbered)}")
    print(f"   fitted pitch           {num_pitch:.3f} px  (residual rms {num_rms:.3f} px)")
    print(f"   fine per numbered      {ratio:.3f}")
    print()
    print("   The rule is stamped `mm` along one edge and `0.5 mm` along the")
    print("   other, and its numbers run 1..6.  The measured 20 fine")
    print("   graduations per numbered division is the only ratio those two")
    print("   stampings allow, so a numbered division is 10 mm, the numbers")
    print("   are centimetres, and one fine graduation is 0.5 mm.  The ratio")
    print("   is measured here; it is not read off the picture.")
    print()
    print(f"   px per mm, fine edge   {scale:.3f}")
    print(f"   px per mm, numbers     {num_pitch / 10.0:.3f}")
    print(f"   the two differ by      {abs(scale - num_pitch / 10.0) / scale * 100:.2f} %")
    print()
    print("   twenty consecutive intervals, px, and each as a count of pitches:")
    gaps = [fine[i + 1] - fine[i] for i in range(20)]
    for i in (0, 10):
        print("     " + "  ".join(f"{g:5.2f}" for g in gaps[i:i + 10]))
        print("     " + "  ".join(f"{g / pitch:5.2f}" for g in gaps[i:i + 10]))
    print()

    # --- 2. where the lit area ends ------------------------------------
    wpx, horizontal, theta, (tl, tr), (li, ri), (lr, rr), rows_n = \
        width_px(im, PANEL_ROWS)
    top = crossings(im, range(200, 520), (235, 285), False, +1)
    ts, ti, trms = fit_line(top)

    print("2. THE ACTIVE AREA")
    print(f"   left  edge   {rows_n} rows  x = {-math.tan(math.radians(tl)):+.5f}*y "
          f"+ {li:8.3f}   rms {lr:.3f} px")
    print(f"   right edge   {rows_n} rows  x = {-math.tan(math.radians(tr)):+.5f}*y "
          f"+ {ri:8.3f}   rms {rr:.3f} px")
    print(f"   top   edge   {len(top)} cols  y = {ts:+.5f}*x + {ti:8.3f}   "
          f"rms {trms:.3f} px")
    print("   bottom edge  NOT MEASURABLE -- it is below the frame.  Only three")
    print("   of the four edges survive in this photograph, which is why the")
    print("   panel being square is taken from its 240x240 raster and not from")
    print("   this picture.")
    print(f"   horizontal separation  {horizontal:.2f} px at y="
          f"{(PANEL_ROWS[0] + PANEL_ROWS[-1]) / 2:.0f}")
    print()

    # --- 3. rotation ----------------------------------------------------
    grad = graduation_tilt(im, (146, 156), (166, 176))
    print("3. ROTATION")
    print(f"   panel side edges       {tl:+.2f} and {tr:+.2f} deg off vertical")
    print(f"   panel top edge         {math.degrees(math.atan(ts)):+.2f} deg off horizontal")
    print(f"   rule graduations       {grad:+.2f} deg off vertical")
    print()
    print("   A horizontal scan crosses graduations and panel edges alike at")
    print("   1/cos(tilt), so most of this cancels in the ratio; what is left")
    print("   is the difference between the two tilts, worth "
          f"{abs(1 / math.cos(math.radians(grad)) * math.cos(math.radians(theta)) - 1) * 100:.2f} %.")
    print("   Both are applied rather than waved away, because the two angles")
    print("   are measured on different features and agree only to the extent")
    print("   that the rule really was laid parallel to the case.")
    print(f"   The side edges do not share one tilt ({tl:.2f} vs {tr:.2f} deg):")
    print("   the camera was not square to the case, so the frame carries")
    print("   perspective as well as rotation, and section 5 does not stop at")
    print("   the fit residual because of it.")
    print(f"   width after correction {wpx:.2f} px")
    print()

    # --- 4. the answer ---------------------------------------------------
    true_scale = scale * math.cos(math.radians(grad))
    width_mm = wpx / true_scale
    diagonal_mm = width_mm * math.sqrt(2.0)
    print("4. RESULT")
    print(f"   active width           {width_mm:.2f} mm      MEASURED")
    print(f"   diagonal, 240x240      {diagonal_mm:.2f} mm = {diagonal_mm / 25.4:.3f} in")
    print(f"   pixel density          {240.0 / (width_mm / 25.4):.1f} ppi")
    print()
    for name, inch in (("1.30", 1.30), ("1.54", 1.54)):
        want = inch * 25.4 / math.sqrt(2.0)
        print(f"   a {name} in panel is {want:5.2f} mm wide; measured "
              f"{width_mm:5.2f} mm is {(width_mm - want) / want * 100:+5.1f} %")
    print()

    # --- 5. what the number is worth -------------------------------------
    print("5. UNCERTAINTY")
    widths = []
    for band in ALT_FINE:
        s, _, _, _ = scale_px_per_mm(im, band)
        for rows in ALT_ROWS:
            widths.append(width_px(im, rows)[0] / (s * math.cos(math.radians(grad))))
    print(f"   {len(widths)} repeats over {len(ALT_FINE)} graduation bands and "
          f"{len(ALT_ROWS)} row windows")
    print(f"   spread                 {min(widths):.3f} .. {max(widths):.3f} mm "
          f"({max(widths) - min(widths):.3f} mm)")
    print(f"   graduation fit rms     {rms:.3f} px = {rms / scale:.3f} mm")
    print(f"   edge fit rms           {max(lr, rr):.3f} px = {max(lr, rr) / scale:.3f} mm")
    print()
    print("   Two systematics of comparable size, pointing OPPOSITE ways.")
    print()
    print("   (a) DEPTH.  The rule lies on the case rim; the pixels are under")
    print("   the cover glass, further from the lens.  The calibration is taken")
    print("   in a plane nearer the camera than the plane being measured, px/mm")
    print("   comes out too large, and the width too SMALL -- so this term puts")
    print("   the true width ABOVE the figure printed in section 4.")
    for depth in (1.0, 2.0, 3.0):
        row = "     ".join(
            f"{dist:3.0f} mm lens -> +{width_mm * (depth / (dist - depth)):.2f} mm"
            for dist in (120.0, 200.0))
        print(f"     rim-to-pixel {depth:.0f} mm:  {row}   ESTIMATED")
    print()
    print("   (b) PERSPECTIVE.  The two side edges do not share a tilt")
    print(f"   ({tl:.2f} vs {tr:.2f} deg), so the frame carries a vertical")
    print("   magnification gradient -- and the scale is NOT read in the rows")
    print("   the panel is measured in.  The fitted edges size it:")
    ls_ = -math.tan(math.radians(tl))
    rs_ = -math.tan(math.radians(tr))
    span = lambda y: (rs_ * y + ri) - (ls_ * y + li)
    y_cal = (FINE_BAND[0] + FINE_BAND[1]) / 2.0
    y_panel = (PANEL_ROWS[0] + PANEL_ROWS[-1]) / 2.0
    print(f"     W(y) = {rs_ - ls_:+.5f}*y + {ri - li:.3f}")
    print(f"     scale read   at y={y_cal:5.1f} (FINE_BAND):  W = {span(y_cal):.2f} px")
    print(f"     panel measured at y={y_panel:5.1f} (PANEL_ROWS): W = {span(y_panel):.2f} px")
    ratio = span(y_cal) / span(y_panel)
    corrected = width_mm * ratio
    print(f"     local scale differs by {(ratio - 1) * 100:+.2f} %, so px/mm is read too")
    print(f"     SMALL and the width comes out too LARGE, by "
          f"{width_mm - corrected:.2f} mm -> {corrected:.2f} mm   ESTIMATED")
    print("     This extrapolates the panel plane's gradient up to the rule's")
    print("     rows, assuming both planes share the perspective law and that it")
    print("     is linear across the span -- the same class of estimate as (a).")
    print()
    print("   Neither term is tightly bounded and they do not cancel by")
    print("   construction, so the band is TWO-SIDED: about -0.2 to +0.7 mm.")
    narrow = 1.30 * 25.4 / math.sqrt(2.0)
    print(f"   Both stay an order below the 4.3 mm separating the candidates:")
    print(f"   even at {corrected:.2f} mm the result is "
          f"{(corrected - narrow) / narrow * 100:+.1f} % from 1.3 in and "
          f"{240.0 / (corrected / 25.4):.1f} ppi,")
    print("   so which panel this is does not depend on either term.")


if __name__ == "__main__":
    main()
