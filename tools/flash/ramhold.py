#!/usr/bin/env python3
"""Load a RAM image onto the watch and keep the serial port open in the same process.

    python3 tools/flash/ramhold.py firmware/build/attadipa.bin [seconds]

Nothing is written to flash. `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP` images live
entirely in IRAM and DRAM, so this route costs nothing to undo — which is why
`docs/ROADMAP.md` prefers it for anything experimental, and why it is the route
this project brought up first.

**Why this exists rather than `esptool load-ram`.** The CLI tool works. What
does not work is letting it exit: the kernel changes the DTR/RTS CDC control
state on the *last* close of a `ttyACM`, and the native USB-Serial/JTAG
peripheral resets the digital core. These are USB control bits, not GPIO0/EN
pins on this board. So `esptool load-ram` reports success and then kills the
image it just loaded, a few milliseconds later, by closing the port. Four runs
were read as "the board rejects RAM images" before the reset was traced to this
host; `rst:0x15 (USB_UART_CHIP_RESET)` is the direct evidence.

Using esptool as a library keeps the port open across the load, so esptool's own
close is never the last one. `docs/research/WAVESHARE_RUNNING_OUR_CODE.md` §2 is
the full account, including the two host-side explanations that were tested and
were *not* it.

**A second reason an image can look dead**, and it stacked with the first: with
`CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM` unset the ROM's putc channel is
disabled and `esp_rom_printf` writes nowhere at all. Use `ESP_LOGx` or `printf`.
Same document, §2.4.

**Two ESP32-S3 boards enumerate as `303a:1001` on this bench** — the watch and a
MeshCore node — so the port is resolved by USB serial and never guessed. Pass
`--port` to override, or `ATTADIPA_WATCH_SERIAL` to name a different unit.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path
from types import SimpleNamespace

# The received Waveshare ESP32-S3-Touch-AMOLED-2.06, by the USB serial its
# USB-Serial/JTAG peripheral reports — see docs/research/BENCH_DEVICES.md.
# `/dev/ttyACM*` numbering is assigned in enumeration order and changes when
# either board is replugged, so it is never the identifier.
DEFAULT_SERIAL = "28:84:85:B2:18:A4"
BY_ID = Path("/dev/serial/by-id")


def resolve_port(serial: str) -> str:
    """The tty for one USB serial, or an explanation. Never a guess.

    Guessing here is not a small mistake: the other board on this bench is a
    MeshCore node, and loading a watch image into it is a confusing failure at
    best. Exiting non-zero is the correct outcome when the watch is unplugged.
    """
    if not BY_ID.is_dir():
        raise SystemExit(f"{BY_ID} does not exist — no udev by-id links on this host")

    matches = [link for link in sorted(BY_ID.iterdir()) if serial in link.name]
    if not matches:
        available = "\n  ".join(link.name for link in sorted(BY_ID.iterdir())) or "(none)"
        raise SystemExit(
            f"no serial device with USB serial {serial}.\n"
            f"Devices present:\n  {available}"
        )
    if len(matches) > 1:
        raise SystemExit(f"{serial} matches more than one device: {matches}")
    return str(matches[0].resolve())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path, help="the PURE_RAM_APP .bin to load")
    parser.add_argument("seconds", nargs="?", type=float, default=15.0,
                        help="how long to watch the console afterwards (default 15)")
    parser.add_argument("--port", default=None,
                        help="serial port, if the USB-serial lookup is not wanted")
    parser.add_argument("--serial", default=DEFAULT_SERIAL,
                        help=f"USB serial of the unit to load (default {DEFAULT_SERIAL})")
    parser.add_argument("--baud", type=int, default=115200)
    # The T-Watch S3 Plus refuses every CDC SET_CONTROL_LINE_STATE request, so
    # every esptool reset strategy fails on it even when the unit is already
    # sitting in download mode. no_reset skips the toggling and talks to the
    # ROM loader that a hand-held BOOT+RESET already left running. See
    # docs/research/TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md.
    parser.add_argument("--connect-mode", default="default_reset",
                        choices=("default_reset", "no_reset", "usb_reset"),
                        help="esptool connect strategy; use no_reset on a unit "
                             "already in download mode (default default_reset)")
    parser.add_argument("--log", type=Path, default=None,
                        help="write the transcript here as well as to stdout")
    args = parser.parse_args()

    if not args.image.is_file():
        raise SystemExit(f"no such image: {args.image}")

    # Imported here rather than at module scope so that --help works on a host
    # with no ESP-IDF environment exported.
    import esptool
    import esptool.cmds

    port = args.port or resolve_port(args.serial)
    print(f"# port {port}  image {args.image}", flush=True)

    esp = esptool.detect_chip(port=port, baud=args.baud,
                              connect_mode=args.connect_mode)
    print(f"# chip {esp.CHIP_NAME}", flush=True)

    # esptool 4.x takes an args namespace with .filename here; 5.x takes the
    # path as a string. ESP-IDF v5.5.5 ships 4.12.0, on which the string form
    # raises AttributeError before a single byte is sent.
    image_arg: object = str(args.image)
    if int(esptool.__version__.split(".")[0]) < 5:
        image_arg = SimpleNamespace(filename=str(args.image))
    esptool.cmds.load_ram(esp, image_arg)
    print("# load_ram returned; port still open, watching", flush=True)

    ser = esp._port  # deliberate: the whole point is not to reopen it
    ser.timeout = 0.05
    deadline = time.time() + args.seconds
    chunks: list[str] = []
    while time.time() < deadline:
        data = ser.read(8192)
        if data:
            text = data.decode("utf-8", "replace")
            chunks.append(text)
            sys.stdout.write(text)
            sys.stdout.flush()

    blob = "".join(chunks)
    if args.log:
        args.log.write_text(blob, encoding="utf-8")

    # A reset banner here means the image died rather than ran, and the first
    # suspect is still the host: something closed the port.
    reset_seen = "rst:0x" in blob or "ESP-ROM:" in blob
    print(f"\n# {len(blob)} chars", flush=True)
    print("# RESET SEEN" if reset_seen else "# no reset banner", flush=True)
    print("# CONSOLE SILENT" if not blob.strip() else "# console spoke", flush=True)
    return 1 if reset_seen or not blob.strip() else 0


if __name__ == "__main__":
    sys.exit(main())
