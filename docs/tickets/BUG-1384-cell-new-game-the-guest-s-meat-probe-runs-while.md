---
id: BUG-1384
title: cell_new_game: the guest's meat probe runs while a caption holds both games (input lock, own_locks 0010): 0/6
status: fixed
kind: test
stage: Cell
reported: 2026-11-20
source: lab rel/0.7.16 20261120-133703 cell_new_game
role: 
versions: unreleased
fixed_in: unreleased
branch: fix/w61-cell
commits: c82df964
tests: cell_new_game
signature:
docs: docs/re/cell.md#cell-eat-gate
related: BUG-0589, BUG-0293
---

## Report
0.7.16 second final check (lab 20261120-133703, lane 4, cold): cell_new_game FAIL "guest: its game ate the meat it swam
into, gone on both (0/6)", each try `points 61->61 here alive there alive (own_locks 0010)` with the guest 1.8-4.6 m
from the meat. History: 14 of 39 failed on all branches since 2026-10-29 (host or guest probe, 0-2 of 6).

## Root cause
Observed (both game logs): the host's probe ate its 6 meats until s 218.27 (food 127); from s 219-220 both games trace
`input_lock=1 busy=cinematic,input_lock` to the end of the test (s 300+), `vote=off`: a caption of the game waiting for
OK, on both games at the same shared total (a new game's caption). The guest's probe ran after the host's, all of it
under that caption; `own_locks 0010` is the avatar's +0x178 lock, which refuses every eat (docs/re/cell.md#cell-eat-gate).
The host's other-diet probe that came later shows the same lock. The play phase's bots confirm captions (play_cell.py);
the probes did not. A test fault (inferred for the earlier 0/6 and 1/6 runs where the logs show the same lock).

## Fix
coop_cell_new_game.py: `close_captions` before each probe try and while swimming: as long as `coop.pool` says cine=1
the probe presses Enter, then the green check (1410 1048), like a player; the first one is shot
(cell_new_game_caption_<who>.png) and logged in the probe line. The 3-of-6 rule is unchanged.

## Verification
no-fail-run: test-only change. Lab: `SPORE_DETECTOR=20 python3 tools/lab.py run cell_new_game --lane N --retry 0
--no-build` PASS (a probe line may show "(a caption was up: confirmed it ...)"). Was waiting for the lab run.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): cell_new_game PASS in 20261122-194210 and 20261123-023643 (its FAIL in 20261121-003202 was the late stage chooser of BUG-1389). Closed.

## History
- 2026-11-20: opened from the second final check of 0.7.16; fixed in the test on fix/w61-cell.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
