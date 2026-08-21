# Vendored: LVGL's `LVGLImage.py`

| | |
|---|---|
| File | `LVGLImage.py` |
| From | `github.com/lvgl/lvgl`, `scripts/LVGLImage.py` |
| Version | **v9.5.0**, commit `85aa60d18` |
| SHA-256 | `c4b59a99104a7592d38b84747296c5e94e86263ca973137b897d295e39b1bff3` |
| Licence | **MIT** — `LVGL-LICENCE.txt` beside this file, copied from the same tree |
| Modified | **no.** Byte-for-byte the upstream file |

## Why it is here rather than referenced

The pipeline that turns `ui/assets/source/` into `ui/assets/generated/` needs
this script to *regenerate*, and regeneration has to work for the next agent and
in CI — neither of which has the development clone at `/root/upstream`. A
checked-out LVGL is a submodule this repository does not have and a download is a
network dependency in a build step. One MIT file, pinned by hash, is cheaper than
either.

`tools/assets/generate_images.py --check` includes this file's hash in its inputs
digest, so replacing it invalidates every generated asset — which is the correct
behaviour, because the encoder changing *is* the asset changing.

## Updating it

Copy the new file, update the hash and the version above, then run
`python3 tools/assets/generate_images.py` and commit whatever moved. If nothing
moves, say so in the commit message; a bump that changes no bytes is still worth
recording, because the next reader will otherwise wonder.

## Its own dependencies

`pypng` and `lz4`, both imported at module scope, both raising a helpful
`ImportError` if missing. They are recorded in
[`docs/research/DEPENDENCIES.md`](../../../docs/research/DEPENDENCIES.md).
Attadipa passes `--compress NONE`, so `lz4` is imported and never used — it is
still required to import the module at all.
