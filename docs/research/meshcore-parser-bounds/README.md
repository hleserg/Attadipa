# MeshCore parser-bounds harness

The executable half of [`../MESHCORE_PARSER_BOUNDS.md`](../MESHCORE_PARSER_BOUNDS.md).
Read that first — this directory is the evidence, not the argument.

**This is not part of any Attadipa build.** No CMake target references it and
none should. It lives under `docs/` so that it cannot be swept into one by a
recursive glob, and it is kept because the alternative to a runnable harness is
a table of results nobody can re-derive when the upstream revision moves.

## What it does

Compiles four upstream MeshCore translation units — `src/Packet.cpp`,
`src/Dispatcher.cpp`, `src/helpers/AdvertDataHelpers.cpp`, `src/Utils.cpp` —
**unmodified**, at whichever revision you name, and feeds their parsers inputs
that are exactly as long as they claim to be.

Each input ends flush against a `PROT_NONE` guard page and the build is under
AddressSanitizer, so a read at `src[len]` is caught either way. Both mechanisms
are needed: ASan names the source line, and ASan alone **does not report a read
at offset 0 of a `malloc(0)`**, which silently passed two real findings before
the guard page was added.

## The shims are not implementations

`shim/` holds `Arduino.h`, `Stream.h`, `SHA256.h` and `AES.h`. They exist so the
upstream files link on a desktop. **`SHA256` returns zeros and `AES128` copies
its block through.** Neither is a cipher, neither may be used as one, and both
say so in their own headers. Nothing measured here depends on what they compute —
only on how many bytes the code around them moves, which is a property of the
loops and not of the block functions they call.

## Running it

```bash
git clone --filter=blob:none https://github.com/meshcore-dev/MeshCore /tmp/meshcore-src
git -C /tmp/meshcore-src fetch origin 05da523ebd32980a1c28b11f2928d351796b9737
git -C /tmp/meshcore-src fetch origin f80d805ee8b20f77ff5b3ca6bc3a9021989aafd2
git -C /tmp/meshcore-src fetch origin pull/3269/head:pr3269   # SHA alone is refused for this one

./build.sh base   d92964352441e53b93e8667b802e04f6e072b39e   # the pin
./build.sh pr3267 05da523ebd32980a1c28b11f2928d351796b9737   # PR #3267 head
./build.sh pr3269 5ebf8ef9cf1a0df28118c47460277857e0e675b2   # PR #3269 head
./build.sh pr3270 f80d805ee8b20f77ff5b3ca6bc3a9021989aafd2   # PR #3270 head
./run.sh

./build-extras.sh base        # the tag names which tree to measure
./build/path_arith            # P3, exhaustive
./build/decrypt_bounds 180    # P4, faults; 176 is clean
```

`MESHCORE_SRC` overrides the clone location. Output goes to `build/`, which is
ignored. Needs `clang++` with AddressSanitizer; measured on clang 18.1.3 under
Ubuntu 24.04 on 2026-08-23, re-run 2026-08-24.

## Checking a new revision

That is the point of keeping it. `./build.sh <tag> <ref> && ./run.sh` answers
"does this revision still over-read" in one command, and
[`../MESHCORE_PARSER_BOUNDS.md`](../MESHCORE_PARSER_BOUNDS.md) §5 makes running
it the entry condition for pinning MeshCore into a local provider.

Two findings are **not** in `run.sh`'s matrix and have to be checked separately —
P3 through `path_arith` and P4 through `decrypt_bounds`. **A green `run.sh` is
not a clean revision.**

Both of those go through `./build-extras.sh <tag>`, and the tag is not optional
decoration. Two ways this directory could have lied about a candidate revision,
both closed on 2026-08-24 after the independent review of
[#160](https://github.com/hleserg/Attadipa/pull/160) found them:

- **It built from `tree-base` whatever you asked for.** `./build.sh cand <sha>`
  followed by a bare `./build-extras.sh` measured the pinned revision and
  reported it without a word. The tag is now an argument, `build.sh` records the
  resolved SHA in `build/tree-<tag>/.revision`, and both binaries print the
  revision they speak for.
- **`path_arith` printed the same answer for every revision.** It hand-copied
  `MAX_PACKET_PAYLOAD`, `MAX_PATH_SIZE` and `isValidPathLen`, so a revision where
  upstream *fixed* `src/Mesh.cpp:172` scored identically to one where it had not.
  The constants and the validator now come from the tree, and the eight lines
  that genuinely cannot be executed on a host are fingerprinted against
  `src/Mesh.cpp` before anything is built — a revision that has touched them is
  refused, with exit 65, naming the digest it wanted and the one it found.
  `#3269`'s head is such a revision, so `./build-extras.sh pr3269` is the
  one-command demonstration that the check is real.
