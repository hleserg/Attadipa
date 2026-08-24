# The generated-tree integrity contract

Two asset trees in this repository are generated and committed rather than
built: `assets/fonts/generated/` and `ui/assets/generated/`. That trade is
deliberate — it keeps Node and Pillow out of every build for files that change
about once a quarter — and it moves the whole problem onto one question: **is
what is committed still what the sources produce?**

Until issue #69 the answer was guarded by a stamp of the *inputs* and a count of
filenames. Both trees would accept a hand-edited output byte and report success,
which was reproduced rather than suspected. These three files are the answer to
that, in ascending order of what they cost to run.

| | Needs | What it settles |
|---|---|---|
| [`stamp.py`](stamp.py) | nothing | the format: an inputs digest **and** a SHA-256 per committed file, so an edited, deleted or unrecorded output fails |
| [`selftest.py`](selftest.py) | nothing | that the two checks above actually reject — 45 mutations, each of the fourteen outputs corrupted in turn |
| [`reproducibility.py`](reproducibility.py) | Node, the pinned converter, Pillow, pypng, lz4 | that the committed bytes are what these inputs *produce*, generated twice from two different absolute paths |

The first two run in every host CI job, as `ui_fonts_are_current`,
`ui_images_are_current` and `ui_generated_outputs_reject_mutations`. The third
does not, and the next section is why.

```bash
python3 tools/integrity/selftest.py

npm install --no-save lv_font_conv@1.5.3
python3 tools/font/fetch_ttf.py --out /tmp/Montserrat-Medium.ttf
python3 tools/integrity/reproducibility.py \
        --ttf /tmp/Montserrat-Medium.ttf --converter ./node_modules/.bin/lv_font_conv
```

## The CI job this is missing, ready to paste — T-128

`reproducibility.py` has been run and it passes: 16 of 16 generated files
identical across two checkout paths and identical to what is committed, in 3.6 s
(recorded on issue #69, in the run that wrote this file). It is **not** wired
into CI, and the reason is a permission rather than a decision: the agent that
wrote it authenticates as a GitHub App whose installation may not write
`.github/workflows`, so the push was refused server-side —

```
refusing to allow a GitHub App to create or update workflow
`.github/workflows/ci.yml` without `workflows` permission
```

That is worth leaving as a refusal rather than a workaround. Whoever adds the
job — the owner, or an orchestrator session whose token may — pastes the block
below into `.github/workflows/ci.yml` under `jobs:`, adds `generated-assets` to
the `evidence` job's `needs:` list, and adds one row to its summary table:

```
            echo "| Committed fonts and icons | REGENERATED — byte-identical from two checkout paths |"
```

It was written against `actionlint 1.7.7` and passes it.

```yaml
  generated-assets:
    name: Generated assets, regenerated from two paths and byte-compared
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7

      # The expensive half of the generated-tree contract, and the only job that
      # needs a converter at all.
      #
      # Every host job checks the committed fonts and icons against the SHA-256
      # in their stamp, which catches a tree that changed. It cannot catch a
      # tree that was never what its inputs produce — a stamp written beside a
      # wrong file records the wrong file faithfully. Only regeneration settles
      # that, and regeneration is evidence only if it is reproducible, which is
      # why this generates twice from two different absolute paths.
      #
      # It is one job rather than a step in every build because it is the only
      # thing here that wants Node, and putting Node between a contributor and a
      # green build for a file that changes once a quarter is the trade the
      # whole committed-output design exists to avoid.

      # The pinned converter, into the runner's temp directory rather than the
      # checkout: reproducibility.py copies the working tree twice, and a
      # node_modules/ inside it would be copied with it.
      - name: Install the pinned lv_font_conv
        run: npm install --prefix "${RUNNER_TEMP}" --no-save --no-audit --no-fund lv_font_conv@1.5.3

      # Pillow from apt for the same reason the other jobs take it from apt —
      # nothing ships it, it is a tool-time dependency. pypng and lz4 are
      # LVGLImage.py's own module-scope imports, needed even with compression
      # off, and they are not packaged, so they come from pip inside a venv that
      # can still see apt's Pillow. The runner's Python is externally managed;
      # installing into it directly is the thing that breaks on a runner bump.
      - name: Install the image pipeline's dependencies
        run: |
          set -euo pipefail
          sudo apt-get update
          sudo apt-get install -y --no-install-recommends python3-pil python3-venv
          python3 -m venv --system-site-packages "${RUNNER_TEMP}/assets-venv"
          "${RUNNER_TEMP}/assets-venv/bin/pip" install --quiet pypng lz4

      # One file out of LVGL rather than the 350 MiB clone the simulator job
      # caches, at the commit cmake/AttadipaLvgl.cmake pins — read from that
      # file, not copied here, and refused unless it hashes to the constant the
      # generator itself will not run without.
      - name: Fetch Montserrat from the pinned LVGL commit
        run: python3 tools/font/fetch_ttf.py --out "${RUNNER_TEMP}/Montserrat-Medium.ttf"

      - name: Regenerate twice and compare against what is committed
        run: |
          set -euo pipefail
          "${RUNNER_TEMP}/assets-venv/bin/python" tools/integrity/reproducibility.py \
            --ttf "${RUNNER_TEMP}/Montserrat-Medium.ttf" \
            --converter "${RUNNER_TEMP}/node_modules/.bin/lv_font_conv"

      # The checkout must be exactly as it was checked out. Nothing above writes
      # into it — both generations happen in copies — and this is what says so
      # rather than assuming it, because a job that quietly regenerated in place
      # would report success for a tree nobody committed.
      - name: The working tree was not touched
        run: |
          set -euo pipefail
          if ! git diff --quiet; then
            echo "reproducibility.py modified the checkout, which it must not:"
            git --no-pager diff --stat
            exit 1
          fi
```

## Why the cheap checks are not enough on their own, and vice versa

A stamp catches a tree that **changed**. It cannot catch a tree that was never
what its inputs produce, because a stamp written beside a wrong file records the
wrong file faithfully — only regeneration settles that.

Regeneration, in turn, is evidence only if it is reproducible, and for the fonts
it was not. `lv_font_conv` writes its own argv into an `Opts:` comment, so the
committed fonts carried one developer's absolute paths and any fresh generation
elsewhere differed in bytes while being identical in every glyph. The
normalization in `tools/font/generate_ui_fonts.py` is what makes the comparison
mean anything; `reproducibility.py` is what stops it quietly rotting again.
