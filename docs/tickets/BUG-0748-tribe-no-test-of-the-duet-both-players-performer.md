---
id: BUG-0748
title: Tribe: no test of the duet (both players' performers on one NPC tribe)
status: open
kind: test
stage: Tribe
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0199
---

## Report
Child of BUG-0199. The co-op duet (tribe.cpp duet_tick: both players' members performing for the same NPC tribe fill
the social mini-game faster, docs/re/tribe.md B) has no two-game test.

Attempt (fix/w46-g4b-coverage, lane 3, out/exp_duet.py on the `tribe` fixture, 2026-11-08):
* ✔ `coop.tribe spawn 3` (3 members each), instruments through the palette's placement on each game: the host
  `coop.tribe buy 4 9 60` + `buy_end`, the guest `coop.tribe buy 5 9 300` + `buy_end`; the host's tool list then has
  the fire pit, the type 4 and the guest's type 5 (BUG-0524 path).
* ✔ The host's member #8 took instrument 4 (`coop.tribe grab 8 1`, held 4 in ~30 s).
* ✘ The guest's member #9 did not take instrument 5 within 30 s (`coop.tribe grab 9 2` on the guest, "grab ordered",
  the host's last order showed action=3 on the fire pit noun 18C8F0C at the old spot): to be looked at (the guest's
  tool index may map to another tool on the host, or the grab came before the host had the guest's tool).
* ✘ The host's social right-click on the NPC hut (play_tribe.Game: social mode, the performer selected, hut1 on
  screen at 1077,563) gave no new order (click_order False): the performance node 0xB46E8803 never started, duet=0.
  Which CitizenAction a social click with an instrument gives is still unknown (docs/re/tribe.md B "not pinned down").

Recipe for the test: as above, then both games right-click the same NPC hut in social mode with their performers
selected; assert on the host both performers in BT node 0xB46E8803 for the same target, the duet counter rising and
the NPC tribe's relationship (CalculateRelationship(npc, player)) reaching the friendly threshold faster than with one
performer; on the guest the same tribe's state (alliance) from the host. Twin: a switch around duet_tick.

## Root cause
A test gap. Blockers: the guest's GrabTool on its new instrument, and the social click with an instrument (above).

## Fix
Not done yet.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
