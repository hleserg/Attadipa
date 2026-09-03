# T-Watch S3 Plus panel and touch first flash — 2026-09-03

Issue [#417](https://github.com/hleserg/Attadipa/issues/417), PR
[#420](https://github.com/hleserg/Attadipa/pull/420). This is the physical
record for the issue-sized first slice, not a claim that every experiment in
`TWATCH_S3_PLUS_BSP_REUSE.md` §11 ran.

## Unit and recovery boundary

- USB serial `DC:B4:D9:18:49:40`; esptool read MAC
  `dc:b4:d9:18:49:40` from an ESP32-S3 revision v0.2 with 8 MB octal PSRAM and
  16 MB flash.
- The verified factory backup remains
  `/home/hleserg/attadipa-bench/twatch-s3-plus_DC-B4-D9-18-49-40_factory_16MB.bin`,
  SHA-256
  `e28f5cdd79552950d7f73fc2776023e297bfcd5dcc320d667ee065b0ebd37202`.
  Restore was **NOT EXECUTED** and was not promised by #417.
- The flash command addressed only that serial identity and wrote the
  bootloader, partition table and application. Esptool verified the hash of
  every write. No eFuse or security setting was changed.
- From the running Attadipa image, esptool's `usb_reset` strategy entered the
  loader without a hand action, read the exact MAC above, uploaded its stub and
  stayed in the loader; `flash_no_reset.py` then attached and wrote the image.
  This was **EXECUTED once**. It proves automatic re-entry from this Attadipa
  image, not from the factory image; the verified manual BOOT route remains the
  recovery path.
- The Waveshare watch and the attached Flipper were not opened by the T-Watch
  flash or console commands.

## Arm C — physical result

Arm C at `4c662c3` was built with ESP-IDF v5.5.5 and flashed. The application
binary is 619,376 bytes, SHA-256
`d413195e8c8b9c0bd162088c0c0543ff3c674816777fcc1aaaacccbd17a6c43f`;
the ELF SHA-256 is
`d311597366aa25aba02dcd86c2a8dc14d80d35df4e9b7782b1d49be2697f12d9`.

The physical screen showed the asymmetric swatch, corner labels, grey ramp and
checkerboard. The serial exercise completed a full flush, a 48×48 partial
flush, rotation 90°→0°, gap 1,1→0,0 and all ten display off/on cycles. It
reported the deliberately short sleep interval as **MEASURED 100 ms**, the
datasheet-conforming interval as **MEASURED 129 ms**, and the reset interval as
**MEASURED 128 ms** with the 100 ms arm-C addition. The final summary reported
`INVON`, 40 MHz SPI, RGB565 byte swap enabled, vendor table not sent, and
`panel exercise passed`. The console then recorded 59 one-second heartbeats
with the free internal heap unchanged at 257,251 bytes. This is a physical
panel **PASS** for arm C.

The retained serial log is
`/home/hleserg/Attadipa/artifacts/watch/i417-arm-c-4c662c3-serial.log`, SHA-256
`0f407c5010264b21f46f872d5eb2743ff2dc2b04ef31f5c2738958afd21517f5`.

## Touch transform — fail, correction, pass

The first physical arm-C run at `484bc73` brought the FT6336U up on the first
`0x38` probe, but the owner observed the yellow marker approximately opposite
the finger. That run is a physical touch-coordinate **FAIL**, not a pass
inferred from the I2C ACK.

The display orientation needs both touch axes mirrored. The correction is at
`firmware/main/twatch_board.cpp:337` — "config.flags.mirror_x = true;" and
`firmware/main/twatch_board.cpp:338` — "config.flags.mirror_y = true;". After
flashing `4c662c3`, the owner followed the four-corners-and-centre check and
reported that the marker coincided with the finger. This is a physical touch
transform **PASS**.

## Deliberately unclaimed

- No photograph was captured: this host exposes no `/dev/video*` device and no
  owner photograph was attached. **NOT EXECUTED — NO CAPTURE PATH.** The panel
  and touch results above are direct live visual observations, not a claim that
  #417's requested photograph artifact exists.
- Arms A and B were not flashed. #417 makes them conditional on arm C showing
  nothing; arm C passed. The broader three-arm matrix, ten cold resets per arm,
  missed-touch-INT edge and failure injections from the research plan remain
  **NOT EXECUTED — HARDWARE REQUIRED** and are not silently folded into this
  first slice.
- Factory-image restore and current measurement remain **NOT EXECUTED**.
