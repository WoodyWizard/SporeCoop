---
id: BUG-1081
title: Tribe: the host's 'Your tribal hut has been destroyed' caption does not hold the guests' games (BUG-0960 covers the tribe knocked out only)
status: open
kind: desync
stage: Tribe
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1076, BUG-0960
---

## Report
Found on fix/w49-fault (BUG-1076), lab 20261113-132309-2 (lane 7, `tribe_own_hut_fall` with the fix): the host's game
showed its own defeat caption "Oh, the humanity! Your tribal hut has been destroyed! Perhaps a fresh start is just the
thing you need." (the key frame 01 of the host; its OK reloads the host's save, inferred from the caption)
while the guest's game played on (`busy -` for 45 s). BUG-0960 (`tribe.gameover_wait`) pauses a guest only while the
host's player tribe is out of members (`player_tribe_out`), not for the hut's defeat. Inferred: the guest plays on
against a host that stands, then the host's reload re-aligns the guest's planet (orders and moves made meanwhile are
lost). Reproduce: `coop.tribe hutfall <players' hut position>` on the host (docs/06-testing.md#testing-own-hut-fall).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
