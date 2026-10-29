---
id: BUG-0271
title: Host faults on a freed cell in cell_fill while the game goes Cell -> Creature (dc04aee5:coop.dll+0x88951)
status: fixed
kind: crash
stage: cell
reported: 2026-10-29
source: lab 20261029-111744 campaign_short
role: host
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w6-cell
commits: eec3280 4761813
tests: cell_evolve_poll (tools/tests/coop_cell_evolve_poll.py)
signature: dc04aee5:coop.dll+0x88951
docs: docs/re/cell.md#cell-pool-freed-between-frames, docs/06-testing.md#testing-cell-evolve-poll
related: BUG-0264
switch: core.stale_local
---

## Report
Lab 20261029-111744 (branch fix/w6-campaign2 f617994, coop.dll built from main 63ed4d6, so with the
BUG-0264 fix): campaign_short on lanes 2 and 4, both FAILED the same way. After the food shortcut
and the two captions of the cell -> creature change, the host (Alice) caught
`0xC0000005 at coop.dll+0x88951` (data = ecx + 0x54) and its console died
("the host plays creature after 71 s", then dead). Logs: out/lab/20261029-111744/campaign_short#1/#2.host.game.log.
In lab 20261029-093233 (the same dll) the same change passed in 5 of 6 four-player games.

## Root cause
Observed: llvm-symbolizer build/coop.dll 0x10088951 = modes::cell_fill (modes.cpp ~486, reading the
cell at ecx). cell_local() = cell_at(cell game +0x411C). The faulting cell's memory is gone: the
game freed the cell (pool) while the co-op adapter still sees the Cell mode / cell game. The stack
already shows creature-planet code (Terrain::cTerrainMapSet::GetHeight, cPlanetModel::ToSurface).
Inferred: same class as BUG-0264 (an object freed before the mode id changes), in Cell on the host.

Observed (fix/w6-cell, lanes 4/5, coop_cell_evolve_poll.py): the cause is not a window with the
mode still Cell. After History's check the game changes the mode to Creature and frees the cell
pool (its own 3.7 MB mapping; `G+0x1C` then reads 0x0000BAAD) inside one step of its loop, between
two of our frames: a per-frame VirtualQuery probe never saw a frame in Cell with the block gone (0
in 6 runs). In that step the creature planet loads and the game pumps window messages; SporeKit runs
the queued main-thread console commands from that pump (WM_SK_PUMP), and the keepalive's network
update runs outside frames. Our state of the last frame (g_mode Cell, g_local = the local cell,
puppet cells) is stale there. With the fix off the test faulted 5 of 6 times: modes::cell_fill
reading +0x54 (the campaign's signature, host and guest, from `coop.status` on a puppet and from
`coop.avatar` / `coop.sync` on the local cell; the stacks hold the planet load's terrain frames as in
the campaign), and once (with only the pool check on) `game::species_profile` reading cell+0xB20
from `coop.avatar`, which asked a creature function on a cell. The campaign polls `coop.status`
every 3 s on the host during the stage change, which is how it hit the step.

## Fix
* `modes::cell_at` reads a cell only from a pool block VirtualQuery still finds committed and
  readable (cached for our own frame scope, on_frame / on_overlay, where no game code runs; checked
  on every call outside it).
* `cell_fill` reads only the live local cell or a live puppet (`modes::cell_usable`).
* coop.cpp `local_now()` / `puppet_now()`: commands (coop.status, coop.avatar, coop.sync, coop.team,
  the bots' targets) and `local_state()` use the local entity and the puppets only while
  `mode_id()` is the frame's mode, the stage is not being left (BUG-0264) and the adapter still finds
  them (a puppet: its `alive`). Inside on_frame they always agree.
* `coop.avatar` calls `game::species_profile` only for creatures (it read cell+0xB20).
* Diagnostics: `coop.cellgone` (pool checks, refusals, local / puppet refusals between frames, the
  per-frame probe), a log line when a freed block is first refused.
* Switch: `coop.fix core.stale_local off` (the old reads).

## Verification
Test `cell_evolve_poll` (2 players, Cell fixture, four console pollers while the host ends the
Cell stage and both enter Creature).
* FAIL (`SPORE_FIX_OFF=core.stale_local`, lane 4): out/lab/20261029-115510 (final code, 2 of 2:
  host crash at modes::cell_fill), 20261029-114941 (1 of 2), 20261029-114314 (the guest faulted in
  cell_fill and froze), 20261029-113722 (host, cell_fill +0x54; that run id was shared by a
  parallel run of an earlier build on lane 5, see docs/06-testing.md#testing-cell-evolve-poll).
* PASS (fix on, lane 5): out/lab/20261029-115614 (final code, 2 of 2; the window was reached:
  local entity refused 1-6 times and a puppet 1-2 times per game), 20261029-114946 (3 of 3),
  20261029-114319.
* campaign_short not rerun on this branch (25 min with 4 games per lane).
* Smoke (lanes 4, 5): out/lab/20261029-120208, 12 of 14; `team` and `select` failed. Reruns:
  20261029-121511 team 2 of 2, select 1 of 2; `select` fails the same way with the fix off
  (20261029-121818, 1 of 3): the known flaky test, BUG-0272. `make test` passes.

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-cell (eec3280, 4761813): no cell, local entity or puppet read between
  frames from a stage the game freed or left; test cell_evolve_poll (FAIL 5 of 6 with the fix off).
