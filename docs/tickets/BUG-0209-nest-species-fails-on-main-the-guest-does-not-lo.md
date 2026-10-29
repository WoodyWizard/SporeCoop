---
id: BUG-0209
title: nest_species fails on main: the guest does not load the host's world a second time after coop.share
status: fixed
kind: bug
stage: Creature
reported: 2026-10-28
source: lab run on lane 6, fix/w4-registry and main 7874654
role: 
versions: main 7874654
fixed_in: 0.7.3.0
branch: main
commits: 1d4cd27
tests: tools/tests/coop_nest_species.py (suite nest_species)
signature:
docs:
related:
switch: core.rejoin_name
---

## Report
`coop_nest_species.py` (suite `nest_species`) fails on lane 6 at "the guest loaded the host's world
again": after the host's `coop.save` and `coop.share` the test waits 120 s for one more
`world: loading ` line in the guest's `log 400` and for both games in the world. Every species
check before and after passes. Seen on fix/w4-registry (out/lab/20261028-031532) and on main
7874654 alone (lab run 20261028-033240 on lane 6). It passed last on fix/w2-creature
(20261027-232407), so waves 2/3 changed something (world sharing, the save box, or the log).

How to reproduce: `tools/lab.py run nest_species --lanes 1 --skip <all other lanes>` on main.

Not analysed yet. Suspects: the check counts lines in the last 400 log lines (a busy log scrolls
the old line out and the count does not grow); the share waits for something new since BUG-0112 /
BUG-0157 / BUG-0019 (one save for waiting shares, no load inside an editor, the evolution
cinematic).

## Root cause
Observed in the host's log (lab 20261028-041508): the test restarts the guest's game, so the guest
sends no BYE and its old entry stays until the 30 s timeout. The guest rejoins before that, and the
unique names of BUG-0141 call it "Bob 2". The test (and any name-keyed state of the player) waits
for "Bob". A player whose game crashed and who rejoins at once got the same new name. The guest did
load the world again; the count of load lines in `log 400` was a second, smaller problem (it stops
growing once older lines scroll out).

## Fix
The host drops an entry of the same name that has been silent for 2 s when a new player with that
name says HELLO (a rejoin), so the name stays; a live player of that name still makes the newcomer
"name 2". Switch `coop.fix core.rejoin_name off` (also `coop.corefix rejoin_name 0`). The test
compares the newest load line instead of counting them.

## Verification
FAIL with `SPORE_FIX_OFF=core.rejoin_name` (lane 2, the guest is not back as Bob). PASS:
lab 20261028-042319 (nest_species, rejoin_loop, rejoin_mobile, creations_rejoin, fix_registry,
multi3, net_robust, civ_shared: 8/8).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on main (orchestrator, after the wave 4 merge).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: commits: filled in (1d4cd27; ef403ce is the ticket only) (BUG-0215).
- 2026-10-28: switch: core.rejoin_name (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
