# What this client does with an answer it cannot read

The evidence behind [MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT](../MESHCORE_OFFLINE_QUEUE_FORWARD_COMPAT.md),
written for [#624](https://github.com/hleserg/Attadipa/issues/624).

```
./run.sh              # print the table
./run.sh > trace.md   # capture it
```

`run.sh` compiles `trace.cpp` against three **production** translation units —
`link/src/meshcore_companion.cpp`, `link/src/link_state.cpp` and
`core/src/mesh_service.cpp` — and runs it. Nothing here is part of an Attadipa
build: `tests/CMakeLists.txt` does not name it and `cmake --build build` does
not compile it. What it measures is therefore the shipping class and not a
model of it, which is the whole reason it exists rather than a diagram.

`trace-2026-09-26.md` is the captured run at
`39898a11171b62577cd4701118d1fc42e4cf97ae`, and the first line of the capture
is that revision. Re-running on a later one is a one-line diff or a finding.

## Reading the table

`draining_` and `pending_push_` have no accessor and were not given one for a
research tool, so each row runs twice against two fresh clients and reads them
through the public surface:

- **next command** — what `next_tx()` yielded on the frame under test. `10` is
  `CMD_SYNC_NEXT_MESSAGE`, `20` is `CMD_GET_BATT_AND_STORAGE`, `-` is silence.
- **drain still open** — a `PUSH_CODE_MSG_WAITING` sent straight afterwards. A
  push that produces a sync at once found `draining_` down; a push swallowed
  with nothing on the wire found it up, because coalescing is the only thing
  that swallows one.
- **unprompted recovery** — a second client, left alone and ticked for a
  simulated minute at 10 ms. The first sync to leave is the recovery, and
  "none in 60 s" means the node keeps whatever it is still holding.

**The diagnosis is columns three and four together.** `next command = -` with
`drain still open = YES` is a client waiting for an answer to a question it
never asked: rows 2, 3, 4 and 7a. The same `YES` on rows 1 and 11 means the
opposite — the client asked again and is properly waiting.

## What is not here

No hardware. This is a host build of this repository's own code driven by
synthetic frames, and no row is a statement about a node, a radio or a board.
Putting `unknown → known → NO_MORE` into a real custom node's queue and
watching the wire is **NOT EXECUTED — HARDWARE REQUIRED**.

The frame layouts for `0x1E` and `0x1F` are transcribed from
[MeshCore PR #3447](https://github.com/meshcore-dev/MeshCore/pull/3447) at
`0d7ba547` and nothing here guesses one. Whether any firmware in the world
actually emits them is a separate question, and the answer today is that
mainline does not.
