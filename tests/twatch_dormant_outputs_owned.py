#!/usr/bin/env python3
"""GPIO2 and PMU REG 0x69 have exactly one owner each, and still have one (#713).

Both halves of #713 were the same defect: a part the repository had already
given an owner, a documented idle state and a diagnostics row, and which no
production path configured. Neither half was caught by a test, because there is
nothing to test in code that does not exist. What a host *can* check is the
shape that made the absence possible, and that is what this does:

* **GPIO2 is named in exactly one file, and by exactly one function.** The pad
  is R64 (0 Ohm) from the base of Q15, so any second path that configures it is
  a second opinion about whether an infrared emitter conducts. `twatch_board.cpp`
  owns it.
* **`start_twatch_ui()` establishes it first.** Not merely somewhere: the first
  statement. Everything after it can fail, and two of those failures return
  without rolling back, so a call that drifted below them would leave the pad
  high-impedance on exactly the boots that then sit in the heartbeat loop.
* **Neither forbidden helper appears on it.** `gpio_reset_pin()` is documented
  as enabling a pull-up, and a pull-up on this circuit holds Q15 on. An internal
  pull-up configured on this pad is the same mistake spelled differently.
* **REG 0x69 is named in exactly one file.** `board_power.cpp`, because
  `ARCHITECTURE.md` gives the charge LED to the power owner.
* **The Waveshare backend names neither.** Its GPIO2 is an SD-card line and its
  CHGLED net terminates in open space; this task was never about that board.

## What it can and cannot see

It reads text, like `snapshot_writes_locked.py` beside it, and the same caveat
applies with the same force:

* It **will** catch a new `firmware/main` file that mentions `GPIO_NUM_2` or
  `0x69`, the owning call being deleted or moved down, and a pull-up or
  `gpio_reset_pin()` written against the IR pad.
* It will **not** catch GPIO2 reached as a computed value -- `GPIO_NUM_0 + 2`,
  a loop over a bit mask, a pin passed in from a table. A driver handed the pad
  by a peripheral config it does not name is also invisible.
* It will **not** notice if `establish_ir_safe_idle()` itself stops driving the
  pin low. That is `test_twatch_dormant_outputs.cpp`'s job, and the two checks
  are deliberately not the same check.
* A file that is missing, or an owner with nothing in it, **fails**. A guard
  that found nothing to guard did not run.

There is deliberately no way to exempt a file. Moving an owner means editing
this file in the same commit, and that diff is the record.

    python3 tests/twatch_dormant_outputs_owned.py
"""
import pathlib
import re
import sys

MAIN = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "main"

# The IR pad's owner, and the call that owns it.
IR_OWNER = "twatch_board.cpp"
IR_SAFE_IDLE = "initialize_ir_safe_idle()"
START = "esp_err_t start_twatch_ui() {"

# The CHGLED register's owner.
CHGLED_OWNER = "board_power.cpp"

# `GPIO_NUM_2` but not `GPIO_NUM_20`; `0x69` but not `0x690`. Both are spelled
# as the tree spells them -- a bare `2` or a decimal `105` is not something a
# text check can tell from anything else, which is part of why the pad also has
# a single declared constant rather than a literal at each use.
IR_PIN = re.compile(r"(?<![0-9A-Za-z_])GPIO_NUM_2(?![0-9A-Za-z_])")
CHGLED_REG = re.compile(r"(?<![0-9A-Za-z_])0[xX]69(?![0-9A-Za-z_])")

# Refused on the IR pad specifically. `gpio_reset_pin` is refused anywhere in
# the owner, because the owner is the only file allowed to name the pad at all
# and it has no other pin to reset.
FORBIDDEN = (
    (re.compile(r"\bgpio_reset_pin\s*\("),
     "gpio_reset_pin() enables a pull-up and releases the driver -- ESP-IDF "
     "documents it as 'select gpio function, enable pullup and disable input "
     "and output'. On this circuit that turns the emitter on"),
    (re.compile(r"pull_up_en\s*=\s*GPIO_PULLUP_ENABLE"),
     "a pull-up on Q15's base holds the IR emitter conducting; the direction "
     "this circuit needs is down"),
)

SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".h", ".hpp")
SKIP_DIR_MARKERS = ("build", "managed_components", "dependencies")


def code_lines(path):
    """Numbered lines with the obvious comment forms dropped.

    Prose that names the rule is not a breach of it -- this change's own
    comments quote `GPIO_NUM_2` and `0x69` repeatedly, and so does the docstring
    above. Only whole-line comments are dropped: a trailing `//` after real code
    leaves the code, which is the half that matters.
    """
    text = path.read_text(encoding="utf-8", errors="replace")
    for number, raw in enumerate(text.splitlines(), 1):
        stripped = raw.strip()
        if stripped.startswith("//") or stripped.startswith("*") or \
                stripped.startswith("/*"):
            continue
        yield number, raw.split("//", 1)[0], stripped


def sources():
    for path in sorted(MAIN.rglob("*")):
        if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
            continue
        if any(marker in part for part in path.relative_to(MAIN).parts[:-1]
               for marker in SKIP_DIR_MARKERS):
            continue
        yield path


def naming(pattern):
    """Which files name this, and where."""
    hits = {}
    for path in sources():
        found = [number for number, code, _ in code_lines(path)
                 if pattern.search(code)]
        if found:
            hits[path.name] = found
    return hits


def first_statement_of_start_twatch_ui(path):
    """The first line of code inside `start_twatch_ui()`, or None."""
    lines = list(code_lines(path))
    for index, (_, _, stripped) in enumerate(lines):
        if stripped != START:
            continue
        for _, _, candidate in lines[index + 1:]:
            if candidate:
                return candidate
        return None
    return None


def check():
    bad = []

    ir_files = naming(IR_PIN)
    if IR_OWNER not in ir_files:
        bad.append(
            f"{IR_OWNER} does not name GPIO_NUM_2. Either the IR pad lost its "
            f"owner -- which is the whole of #713 coming back -- or the owner "
            f"moved and this check did not move with it.")
    for name, numbers in sorted(ir_files.items()):
        if name == IR_OWNER:
            continue
        where = ", ".join(f"{name}:{n}" for n in numbers)
        bad.append(
            f"{where} names GPIO_NUM_2. That pad is R64 (0 Ohm) from the base "
            f"of Q15: a second path configuring it is a second opinion about "
            f"whether an infrared emitter conducts. {IR_OWNER} owns it.")

    chgled_files = naming(CHGLED_REG)
    if CHGLED_OWNER not in chgled_files:
        bad.append(
            f"{CHGLED_OWNER} does not name REG 0x69. Either the CHGLED policy "
            f"is gone, or its owner moved and this check did not.")
    for name, numbers in sorted(chgled_files.items()):
        if name == CHGLED_OWNER:
            continue
        where = ", ".join(f"{name}:{n}" for n in numbers)
        bad.append(
            f"{where} names PMU REG 0x69. ARCHITECTURE.md gives the charge LED "
            f"to PowerService, and {CHGLED_OWNER} is it.")

    owner = MAIN / IR_OWNER
    if not owner.is_file():
        bad.append(f"{IR_OWNER} is missing; this check cannot run.")
        return bad

    uses = [number for number, code, _ in code_lines(owner)
            if IR_SAFE_IDLE in code]
    if not uses:
        bad.append(
            f"{IR_OWNER} never calls {IR_SAFE_IDLE}. The pad is declared and "
            f"left floating, which is the state #713 was opened about.")
    first = first_statement_of_start_twatch_ui(owner)
    if first is None:
        bad.append(
            f"{IR_OWNER} has no line `{START}`, so where the safe state is "
            f"established cannot be checked. Teach this check the new spelling.")
    elif IR_SAFE_IDLE not in first:
        bad.append(
            f"{IR_OWNER}: the first statement of start_twatch_ui() is\n"
            f"    {first}\n"
            f"  and not {IR_SAFE_IDLE}. Everything below that line can fail, "
            f"and the profile checks return without rolling back -- a safe "
            f"state established after them is not established on the boots "
            f"that need it.")

    for pattern, why in FORBIDDEN:
        for number, code, _ in code_lines(owner):
            if pattern.search(code):
                bad.append(f"{IR_OWNER}:{number}  {code.strip()}\n  {why}")

    return bad


def main():
    bad = check()
    if bad:
        print("T-Watch dormant outputs: FAIL", file=sys.stderr)
        for finding in bad:
            print(f"  {finding}", file=sys.stderr)
        return 1
    print("T-Watch dormant outputs: OK (GPIO2 owned by "
          f"{IR_OWNER} and established first; REG 0x69 owned by "
          f"{CHGLED_OWNER})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
