# minmea, vendored

`minmea.c` and `minmea.h` in this directory are **upstream's files, unmodified**,
from [kosma/minmea](https://github.com/kosma/minmea) at commit
`2dd2cd11a359de5583e68053182d5bbf29725934`. Nothing in Attadipa edits them, and
nothing should — `docs/research/REUSE_LEDGER.md:529` — "**Decision:** `WRAP` —
take `minmea.c` / `minmea.h` unmodified at" — chose this library *because*
keeping it byte-identical means the known open bug arrives as a version bump
rather than as a merge.

| File | SHA-256 |
| --- | --- |
| `minmea.c` | `3b30b322e513389b39a4e9610120a67cd8d1f8521bf541cff724b8e21e20fbe1` |
| `minmea.h` | `d7f7817c4165d1867adc84c3932c73019ce73e1b1a2f4a0ec42c2b8cfa9dfce1` |
| `COPYING` | `ee820ff0db4ce628569e0975ac27dc926052a9f85d102b101edb104311ef4d90` |
| `LICENSE.MIT` | `92c301a7d025048ae6a2f36669c5bbca5f448f65f20249f1e3cb88cdc3816b49` |
| `LICENSE.grants` | `55c4f57f78f3d7851ec2ca074b2bacd8f77e88cf9404f351fdf4b8f7f9392aaf` |

## The licence is three files, not one

`COPYING` is WTFPL-2.0. `LICENSE.grants` is the author's explicit grant of MIT
or LGPL-3.0-or-later at the recipient's option, and `LICENSE.MIT` is the text
Attadipa takes. All three are copied because the grant is worthless without the
document it grants *from* and the text it grants *to*.

## What the wrapper owns, and why there is one

Upstream parses one NUL-terminated sentence at a time and stops there. Line
assembly, the 82-byte cap, strict checksum verification and every value check
are Attadipa's — `gnss/src/nmea_receiver.cpp` — which is the same split the
reuse ledger asked for, and it matters: the ledger's own upstream-issue list
records a checksum computed from a fixed offset rather than from the located
`$`, in a project whose assembler was the buggy part.

Two rules the wrapper follows that come straight from that list:

- **A field is present only when `scale > 0`**, never `scale != 0`. minmea's
  overflow guard can be defeated into producing a negative scale
  (kosma/minmea#104, open).
- **`-ffast-math` is a correctness hazard here**, not a performance knob. This
  tree never enables it and this parser is one of the reasons.

## `timegm`

`minmea.c:671` calls `timegm()`. The toolchain Attadipa ships with does not have
it: `CONFIG_LIBC_NEWLIB=y` on both boards, and the esp32s3 newlib `libc.a`
defines `mktime` and no `timegm` (checked with `nm --defined-only`). Upstream's
own answer is to build with `-Dtimegm=mktime`, and `gnss/CMakeLists.txt` does
exactly that on this target and only this target.

That substitution is wrong wherever the process is not in UTC — and it is
harmless here because **Attadipa calls neither `minmea_gettime` nor
`minmea_getdatetime`.** The wrapper converts `minmea_date` and `minmea_time`
itself, through `core::wall_time_from_civil`, which is integer, pure, and
already the conversion the clock and the provisioning entry use. If a future
caller wants minmea's own time helpers, that caller has to solve `timegm` first
rather than inherit a silently local-time answer.

## Bumping it

Replace both source files with the new upstream pair, update the SHA-256 table
above and the row in `docs/research/DEPENDENCIES.md`, and
re-run `test_nmea_receiver` — which asserts against sentences a real receiver
emitted, not against minmea's own suite. Testing a copied implementation proves
nothing about the caller; the wrapper's tests are the ones that do.
