---
id: BUG-0509
title: Cell: a knocked-out cell can still bite, kill and take damage while it waits for a revive
status: fixed
kind: bug
stage: Cell
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: both
versions: 0.7.3.6
fixed_in: 0.7.4
branch: fix/w40-guest-combat
commits: 75c44630, 0785a1cd, 33104459
tests: coop_cell_ko_bite.py
signature:
docs: docs/re/cell.md#cell-ko-attacks
related: BUG-0070, BUG-0237, BUG-0428
---

## Report
Host's mark (0.7.3.6, report 20261103-190600): 'after KO I could still damage with my corpse's mouth and kill and get damaged'; a later video from the same group: 'I got knocked out but I could still attack'. Host log: `ko: own cell state 1` at 19:03:51 (knocked out, 10 s), `state 3` (respawned) at 19:04:01; Cell stage, two players.
Target: 0.7.4 (Cell).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the code (rel/0.7.4 cellsync.cpp go_down / downed_tick): a downed cell gets hp 1, dead/dying 0, the invulnerable flag +0x111 = 1, opacity, and is held at its spot (stop_at), but nothing stops its own bites / spikes / poison: the game's attack code still runs for the avatar. Inferred: 'got damaged' is the hp bar moving (hp forced back to 1 every frame) or damage applied before the flag. The knockout is the mod's own rule (class BUG-0428): it should also block the avatar's attacks (or set the game's own 'dying' state with input off) while downed. Size: S.
Observed (lane 2, fix off): a downed guest's bites through the game's Bite killed a host creature (6 -> 3 -> 0 in the
host's game, MSG_CELL_HIT), a downed host's bites the same in its own game. Not looked at: 'got damaged' (inferred:
the health ring; the cell is invulnerable, hp forced to 1).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
`cell.ko_no_bite` (cellsync.cpp): while downed the avatar's Bite returns false, DamageCell and Break with the avatar
as attacker do nothing; the host refuses MSG_CELL_HIT from a guest it knows is knocked out (KO_OUT). Off: 0.7.3.6.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
coop_cell_ko_bite.py (suite cell_ko_bite).
* FAIL with `SPORE_FIX_OFF=cell.ko_no_bite`: out/lab/20261103-173514 (guest and host: the bitten creature killed).
* PASS: out/lab/20261103-173638 (6 -> 6 on the host, 5 bites blocked each).
* Not reached by the test: the host's refusal of a knocked-out guest's MSG_CELL_HIT.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage).
- 2026-11-03: fixed on fix/w40-guest-combat (cell.ko_no_bite).
- 2026-11-03: released in 0.7.4.
