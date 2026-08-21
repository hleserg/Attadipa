"""Prove the catalogue checks can fail, and fail for the right reason.

A check nobody has seen reject anything is a check nobody knows works. This
repository has been bitten by that shape already — `WILL_FAIL TRUE` on a build
test passes on *any* non-zero exit, so a renamed target reported success
(tests/expect_build_failure.cmake). The same discipline applies here: each
fixture in tests/l10n/fixtures/ is a specific mistake, and this asserts both
that it is rejected and that the message names the mistake rather than something
else that happened to go wrong.

  python3 tools/l10n/selftest.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from catalogue import REPO_ROOT, CatalogueError, load  # noqa: E402
import check_glyphs  # noqa: E402

FIXTURES = REPO_ROOT / "tests" / "l10n" / "fixtures"

# fixture -> a fragment the message must contain. The fragment is the *reason*,
# not just any word from the output: a fixture that is rejected for the wrong
# reason is a failing test here, exactly as a build that fails for the wrong
# reason is in expect_build_failure.cmake.
PARSE_CASES = {
    "duplicate_id.toml":     "settings_title",
    "missing_ru.toml":       "has no 'ru' entry",
    "ru_other.toml":         "never be shown",
    "missing_ru_few.toml":   "missing ['few']",
    "format_mismatch.toml":  "same placeholders",
    "empty_value.toml":      "is empty",
    "bad_identifier.toml":   "not a usable identifier",
}

# Rejected by the glyph check rather than by the parser: the file is valid TOML
# and a valid catalogue, and still cannot be drawn.
GLYPH_CASES = {
    "glyph_outside_charset.toml": ("U+4E16", "U+1F30D"),
}


def run():
    failures = []

    for name, expected in PARSE_CASES.items():
        path = FIXTURES / name
        if not path.exists():
            failures.append(f"{name}: fixture is missing")
            continue
        try:
            load(path)
        except CatalogueError as exc:
            message = str(exc)
            if expected not in message:
                failures.append(
                    f"{name}: rejected, but not for the reason this fixture is about.\n"
                    f"    expected the message to contain: {expected!r}\n"
                    f"    got: {message}"
                )
            else:
                print(f"  ok  {name:<28} rejected: {message.splitlines()[0][:78]}")
        else:
            failures.append(f"{name}: ACCEPTED, and it must not be. The check is not working.")

    for name, expected_fragments in GLYPH_CASES.items():
        path = FIXTURES / name
        if not path.exists():
            failures.append(f"{name}: fixture is missing")
            continue
        try:
            load(path)
        except CatalogueError as exc:
            failures.append(f"{name}: rejected by the parser ({exc}); it should reach the "
                            f"glyph check, which is the thing being tested.")
            continue
        import io
        import contextlib
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check_glyphs.main([str(path)])
        output = buffer.getvalue()
        if code == 0:
            failures.append(f"{name}: the glyph check ACCEPTED characters outside the subset.")
            continue
        missing = [f for f in expected_fragments if f not in output]
        if missing:
            failures.append(f"{name}: rejected, but did not name {missing}.\n    got: {output}")
        else:
            print(f"  ok  {name:<28} rejected: names {', '.join(expected_fragments)}")

    if failures:
        print("\nl10n selftest FAILED:\n", file=sys.stderr)
        for failure in failures:
            print(f"  * {failure}", file=sys.stderr)
        return 1

    print(f"\nl10n selftest: {len(PARSE_CASES) + len(GLYPH_CASES)} deliberate mistakes, "
          f"all rejected, each for its own reason")
    return 0


if __name__ == "__main__":
    sys.exit(run())
