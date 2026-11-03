---
id: BUG-0509
title: Cell: a knocked-out cell can still bite, kill and take damage while it waits for a revive
status: open
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: both
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0070, BUG-0237, BUG-0428
---

## Report
Host's mark (0.7.3.6, report 20261103-190600): 'after KO I could still damage with my corpse's mouth and kill and get damaged'; a later video from the same group: 'I got knocked out but I could still attack'. Host log: `ko: own cell state 1` at 19:03:51 (knocked out, 10 s), `state 3` (respawned) at 19:04:01; Cell stage, two players.
Target: 0.7.4 (Cell).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the code (rel/0.7.4 cellsync.cpp go_down / downed_tick): a downed cell gets hp 1, dead/dying 0, the invulnerable flag +0x111 = 1, opacity, and is held at its spot (stop_at), but nothing stops its own bites / spikes / poison: the game's attack code still runs for the avatar. Inferred: 'got damaged' is the hp bar moving (hp forced back to 1 every frame) or damage applied before the flag. The knockout is the mod's own rule (class BUG-0428): it should also block the avatar's attacks (or set the game's own 'dying' state with input off) while downed. Size: S.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
