# Attadipa

**The project's rules are in `CLAUDE.md` at the repository root. Read it first.**
It carries the specification in force, the rule that no hardware fact is used
before it is traced to a datasheet or vendor source, the rule that a test which
did not run on a board is never written `PASS`, and how work arrives and is
handed over. This file is a pointer rather than a copy, so that the copy cannot
drift from the original.

`AGENTS.md` describes the two tools that generate files like this one: `rtk`,
which compacts command output, and `graft`, the code graph under `graft/`.
