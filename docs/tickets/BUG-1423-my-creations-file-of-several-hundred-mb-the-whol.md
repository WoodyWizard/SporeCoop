---
id: BUG-1423
title: My Creations file of several hundred MB: the whole size was allocated before the size check (bad_alloc in the 32-bit game, CORE-11)
status: fixed
kind: crash
stage: All
reported: 2026-11-22
source: audit
role: both
versions: 0.7.16.1
fixed_in: unreleased
branch: fix/w63-net
commits: d4c50db9
tests: creations_big_file, creations_big_file_off
signature:
docs:
related:
---

## Report
CORE-11 of the external static audit of 0.7.15.5 (out/research/ext-audit-0.7.15.5.md; verified row in
out/research/ext-audit-verify-net-core.md). Unchanged on rel/0.7.16 (creations.cpp read_file, lines 260-273 at 5376d388).
A player with a My Creations file (EditorSaves.package) of several hundred MB (heavy Sporepedia use) either has the
whole file zero-filled every 10 s for nothing (the co-op window's creation list rereads it) or the game ends with an
unhandled std::bad_alloc where the 32-bit address space has no such block.

## Root cause
Inferred from the code: `read_file` ignored `GetFileSizeEx`'s result and called `out.resize(size)` before the
`size < 256 MB` check. Above 2 GB `resize` throws std::length_error (the vector's max_size in a 32-bit process), below
it std::bad_alloc when no block of that size is free; nothing catches either in the mod's tick: the game ends.

## Fix
A failed size or one of 256 MB and more is refused before anything is allocated; the log says it once per size
("... not read, own creations are off (BUG-1423)"). Switch `coop.fix creations.size_first off`: the allocation first
(0.7.16).

## Verification
Open, waiting for the lab run. `tools/lab.py run creations_big_file creations_big_file_off`: the guest's
EditorSaves.package is replaced by a sparse 2.2 GB file for ~35 s; creations_big_file must PASS (the game goes on,
the creations come back), creations_big_file_off (the twin) must FAIL (the guest's game ends in the read).

- 2026-11-23 (0.7.16.1 final check): creations_big_file PASS in 20261123-152921 (aad722ac).

## History
- 2026-11-23: closed on the 0.7.16.1 final check lab evidence.
- 2026-11-22: opened.

- 2026-11-22: renumbered from BUG-9308 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9308.
