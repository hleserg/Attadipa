# Site design captures

The twelve `docs/assets/design-*.png` files are byte-identical copies of the
corresponding files in `pics/`. Pages publishes only `docs/`; it cannot serve
repository-root `pics/` through a relative URL.

[Canonical capture provenance](../../pics/README.md#browser-design-study-captures)
records the browser study revision, sample-data boundary, geometry, reproduction
commands and SHA-256 hashes. These are not physical-watch photographs or
firmware screenshots. No image was generated, retouched or resampled for the
site. Keep each pair identical when replacing a capture; check with `cmp`.

Nunito Sans is reused directly from `docs/ui/prototype/NunitoSans.ttf` through
the stylesheet. Its [OFL licence](../ui/prototype/OFL.txt) and existing font
provenance remain alongside the original file. No additional font copy or
external font service is needed.
