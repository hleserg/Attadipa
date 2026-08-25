#!/usr/bin/env python3
"""Prove check_inventory.py notices what it claims to notice.

The same argument as the other self-tests in `tools/`, and a sharper one here.
`check_inventory.py` exists *because* a checker that has only ever agreed with
itself is indistinguishable from a checker that returns 0 — that is the defect
issue #68's follow-up reported, one level up. A completeness check nobody has
ever seen fail would be the same mistake wearing a different hat.

So each case below breaks the inventory in a way somebody will actually break
it — deleting an entry point, renaming one, getting an argument position wrong,
adding a property LVGL does not have — and fails if `check_inventory.py`
shrugged. The first case is the exact one from the issue: take
`lv_obj_set_ext_click_area` back out and the check must name it.

Needs the LVGL sources, for the same reason `check_inventory.py` does. Without
them it exits 2 rather than 0, so a run that could not check anything cannot be
mistaken for a run that found nothing.
"""

from __future__ import annotations

import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_inventory                # noqa: E402
import lvgl_inventory as inventory     # noqa: E402


def without_entry_point(name: str):
    def mutate() -> None:
        del inventory.ENTRY_POINTS[name]
    return mutate


def with_entry_point(name: str, positions):
    def mutate() -> None:
        inventory.ENTRY_POINTS[name] = positions
    return mutate


# `lvgl_inventory.ENTRY_POINTS` is built from the property tuples at import
# time, so changing a tuple in a live process would leave the entry points it
# already generated behind — and the case would pass for the wrong reason.
# These two put the module into the state a *fresh import* of the edited file
# would produce, which is what a person changing that tuple actually gets.
_STYLE_SPELLINGS = ("lv_obj_set_style_{}", "lv_style_set_{}")


def without_property(table: str, prop: str):
    def mutate() -> None:
        current = getattr(inventory, table)
        setattr(inventory, table, tuple(p for p in current if p != prop))
        for spelling in _STYLE_SPELLINGS:
            inventory.ENTRY_POINTS.pop(spelling.format(prop), None)
    return mutate


def with_property(table: str, prop: str):
    def mutate() -> None:
        setattr(inventory, table, getattr(inventory, table) + (prop,))
        kind = (inventory.LENGTH if table == "LENGTH_PROPERTIES"
                else inventory.DURATION)
        for spelling in _STYLE_SPELLINGS:
            inventory.ENTRY_POINTS[spelling.format(prop)] = ((1, kind),)
    return mutate


def without_excuse(name: str):
    def mutate() -> None:
        del inventory.NOT_A_DESIGN_VALUE[name]
    return mutate


def without_other_argument(name: str):
    def mutate() -> None:
        del inventory.OTHER_ARGUMENTS[name]
    return mutate


# Each: what it breaks, the mutation, and a fragment the complaint must contain.
MUTATIONS = (
    (
        "the entry point issue #68's follow-up found missing",
        without_entry_point("lv_obj_set_ext_click_area"),
        "lv_obj_set_ext_click_area: unclassified",
    ),
    (
        "a style property dropped from the length list",
        without_property("LENGTH_PROPERTIES", "radius"),
        "does not classify it",
    ),
    (
        "a style property that LVGL does not have",
        with_property("LENGTH_PROPERTIES", "corner_softness"),
        "declares no numeric setter for it",
    ),
    (
        "an entry point LVGL renamed out from under us",
        with_entry_point("lv_obj_set_wibble", ((1, inventory.LENGTH),)),
        "does not declare it",
    ),
    (
        "an argument position past the end of the signature",
        with_entry_point("lv_obj_set_width", ((7, inventory.LENGTH),)),
        "arguments",
    ),
    (
        "an argument position that is not a number",
        with_entry_point("lv_obj_align", ((1, inventory.LENGTH),)),
        "which is not a number",
    ),
    (
        "a numeric argument nobody said anything about",
        without_other_argument("lv_table_set_column_width"),
        "nothing says what it is",
    ),
    (
        "a call that stopped being excused and was not classified instead",
        without_excuse("lv_bar_set_value"),
        "lv_bar_set_value: unclassified",
    ),
    (
        "a call in both tables at once",
        with_entry_point("lv_bar_set_value", ((1, inventory.LENGTH),)),
        "it takes a design value or it does not",
    ),
)


def snapshot() -> dict:
    return {
        "ENTRY_POINTS": copy.deepcopy(inventory.ENTRY_POINTS),
        "NOT_A_DESIGN_VALUE": copy.deepcopy(inventory.NOT_A_DESIGN_VALUE),
        "OTHER_ARGUMENTS": copy.deepcopy(inventory.OTHER_ARGUMENTS),
        "LENGTH_PROPERTIES": inventory.LENGTH_PROPERTIES,
        "DURATION_PROPERTIES": inventory.DURATION_PROPERTIES,
    }


def restore(saved: dict) -> None:
    inventory.ENTRY_POINTS.clear()
    inventory.ENTRY_POINTS.update(saved["ENTRY_POINTS"])
    inventory.NOT_A_DESIGN_VALUE.clear()
    inventory.NOT_A_DESIGN_VALUE.update(saved["NOT_A_DESIGN_VALUE"])
    inventory.OTHER_ARGUMENTS.clear()
    inventory.OTHER_ARGUMENTS.update(saved["OTHER_ARGUMENTS"])
    inventory.LENGTH_PROPERTIES = saved["LENGTH_PROPERTIES"]
    inventory.DURATION_PROPERTIES = saved["DURATION_PROPERTIES"]


def main(argv: list[str]) -> int:
    root = check_inventory.find_lvgl(argv)
    if root is None or not (root / "src").is_dir():
        print("selftest_inventory: no LVGL source tree found, so nothing was "
              "checked. Pass its path or set ATTADIPA_LVGL_SOURCE_DIR.",
              file=sys.stderr)
        return 2

    failures = 0

    clean = check_inventory.check(root)
    if clean:
        print("FAIL: the unmodified inventory is not clean, so every case "
              "below would pass for the wrong reason:", file=sys.stderr)
        for problem in clean:
            print(f"    {problem}", file=sys.stderr)
        return 1

    saved = snapshot()
    for name, mutate, expected in MUTATIONS:
        try:
            mutate()
            problems = check_inventory.check(root)
        finally:
            restore(saved)
        if not problems:
            print(f"FAIL: {name} — check_inventory.py found nothing",
                  file=sys.stderr)
            failures += 1
            continue
        if not any(expected in problem for problem in problems):
            print(f"FAIL: {name} — no {expected!r} in:", file=sys.stderr)
            for problem in problems:
                print(f"    {problem}", file=sys.stderr)
            failures += 1

    still_clean = check_inventory.check(root)
    if still_clean:
        print("FAIL: the inventory was not restored after the mutations, so "
              "these cases are not independent:", file=sys.stderr)
        for problem in still_clean:
            print(f"    {problem}", file=sys.stderr)
        failures += 1

    if failures:
        print(f"{failures} inventory self-test case(s) failed", file=sys.stderr)
        return 1

    print(f"check_inventory self-test: {len(MUTATIONS)} mutation(s) caught, "
          f"as intended")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

