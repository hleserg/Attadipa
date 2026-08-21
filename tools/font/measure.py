"""Measure what an embedded Attadipa font actually costs in flash.

Final section 51 asks for a *measured* generated font size, not an estimate, so
this compiles the generated C with the ESP32-S3 compiler and reads .rodata out
of the object file. The C file's size on disk is not the answer: it is ASCII
hex, roughly three bytes of text per byte of flash.

Two things this exists to catch, both discovered by running it:

  * the variable-font default instance. Both candidate families ship from Google
    Fonts as variable fonts only, and lv_font_conv takes the default instance.
    Nunito Sans defaults to wght 200 (ExtraLight), which is not a UI weight and
    is unreadable at 14 px on a watch. Instantiating first is not an
    optimisation, it is the difference between the font you chose and a
    different one.

  * bpp against compression. They interact, and the interaction is not
    monotonic: compression helps most at high bpp, and can *cost* at bpp 1.

Usage:
  python3 tools/font/measure.py --fonts DIR --lv-font-conv PATH --lvgl DIR \\
                                --lv-conf FILE --out results.csv
"""
import argparse, csv, os, subprocess, sys, shutil, tempfile
from fontTools.ttLib import TTFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from charset import RANGES, as_lv_font_conv_ranges, codepoints

SIZES = [14, 16, 20, 24, 28, 36, 48]
BPPS = [1, 2, 4]


def find_xtensa_gcc():
    hits = []
    root = os.path.expanduser("~/.espressif/tools/xtensa-esp-elf")
    for dirpath, _dirs, files in os.walk(root):
        if "xtensa-esp32s3-elf-gcc" in files:
            hits.append(os.path.join(dirpath, "xtensa-esp32s3-elf-gcc"))
    return sorted(hits)[-1] if hits else None


def coverable_ranges(font_path):
    """The ranges this font can supply, and the ones it cannot.

    lv_font_conv refuses a range it cannot fill at all, which is the right
    behaviour and the reason a font that is missing something fails loudly
    rather than shipping boxes. To *measure* such a font at all, the empty
    ranges have to come out -- and every one that comes out is printed, because
    a measurement of a smaller charset reported as if it were the charset is
    the quiet lie this whole file exists to avoid.
    """
    have = set()
    font = TTFont(font_path, fontNumber=0, lazy=True)
    for table in font["cmap"].tables:
        have.update(table.cmap.keys())

    keep, dropped = [], []
    for lo, hi, why in RANGES:
        present = [cp for cp in range(lo, hi + 1) if cp in have]
        spec = f"0x{lo:04X}-0x{hi:04X}" if lo != hi else f"0x{lo:04X}"
        if present:
            keep.append(spec)
            if len(present) != hi - lo + 1:
                dropped.append((spec, why, f"partial: {len(present)} of {hi - lo + 1}"))
        else:
            dropped.append((spec, why, "no glyphs at all"))
    return keep, dropped, sum(1 for cp in codepoints() if cp in have)


def generate(conv, font, size, bpp, compress, out_c, ranges=None):
    args = [conv, "--font", font]
    for r in (ranges if ranges is not None else as_lv_font_conv_ranges()):
        args += ["--range", r]
    args += ["--size", str(size), "--bpp", str(bpp), "--format", "lvgl", "-o", out_c]
    if not compress:
        args.append("--no-compress")
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"lv_font_conv failed for {font} {size}/{bpp}:\n{p.stdout}{p.stderr}")


def rodata_bytes(gcc, size_tool, out_c, lvgl_dir, conf_dir):
    obj = out_c[:-2] + ".o"
    p = subprocess.run([gcc, "-c", "-Os", "-mlongcalls", f"-I{conf_dir}", f"-I{lvgl_dir}",
                        "-DLV_CONF_INCLUDE_SIMPLE", out_c, "-o", obj],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"compile failed:\n{p.stdout}{p.stderr}")
    p = subprocess.run([size_tool, "-A", obj], capture_output=True, text=True)
    for line in p.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0] == ".rodata":
            return int(parts[1])
    raise RuntimeError("no .rodata in " + obj)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", required=True, help="directory of .ttf files to measure")
    ap.add_argument("--lv-font-conv", required=True)
    ap.add_argument("--lvgl", required=True, help="LVGL source tree (the pinned one)")
    ap.add_argument("--lv-conf", required=True, help="an lv_conf.h to compile against")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    gcc = find_xtensa_gcc()
    if not gcc:
        sys.exit("No xtensa-esp32s3-elf-gcc found. This measures the target, not the host.")
    size_tool = gcc.replace("-gcc", "-size")

    work = tempfile.mkdtemp(prefix="attadipa-font-")
    conf_dir = os.path.join(work, "conf")
    os.makedirs(os.path.join(conf_dir, "lvgl"))
    shutil.copy(a.lv_conf, os.path.join(conf_dir, "lv_conf.h"))
    os.symlink(os.path.join(os.path.abspath(a.lvgl), "lvgl.h"),
               os.path.join(conf_dir, "lvgl", "lvgl.h"))

    fonts = sorted(f for f in os.listdir(a.fonts) if f.endswith(".ttf"))
    rows = []
    for f in fonts:
        path = os.path.join(a.fonts, f)
        ranges, dropped, n = coverable_ranges(path)
        print(f"\n{f[:-4]}: {n} of {len(codepoints())} codepoints")
        for spec, why, how in dropped:
            print(f"  DROPPED {spec:<13} {how:<22} ({why})")
        for size in SIZES:
            for bpp in BPPS:
                for compress in (True, False):
                    out_c = os.path.join(work, "f.c")
                    generate(a.lv_font_conv, path, size, bpp, compress, out_c, ranges)
                    rodata = rodata_bytes(gcc, size_tool, out_c, os.path.abspath(a.lvgl), conf_dir)
                    rows.append(dict(font=f[:-4], px=size, bpp=bpp,
                                     compressed="yes" if compress else "no",
                                     rodata=rodata, glyphs=n,
                                     per_glyph=round(rodata / n, 1)))
                    print(f"  {f[:-4]:22s} {size:>3} px  bpp {bpp}  "
                          f"{'compressed' if compress else 'raw       '}  {rodata:>7} B", flush=True)

    with open(a.out, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)
    print(f"\n{len(rows)} measurements -> {a.out}")
    print(f"compiler: {gcc}")


if __name__ == "__main__":
    main()
