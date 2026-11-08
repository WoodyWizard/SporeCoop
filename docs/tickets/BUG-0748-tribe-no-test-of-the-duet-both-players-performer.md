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
branch: fix/w46-b1-tribe
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
A test gap. Attempts on fix/w46-b1-tribe (lane 7, the `tribe` fixture, out/b1tribe/exp_duet.py, 2026-11-08):
* ✔ The guest member's instrument (blocker 1 of the first attempt) is solved by buying the instruments the game's way:
  `coop.tribe buy 4 9 60` + `buy_end`, `buy 5 9 300` + `buy_end` on the host, then `coop.tribe grab <id> <tool index>`
  on the host for the host's and the guest's member: both held their instrument (4 / 5) within 4 s (lab
  20261108-151615 vs -153517). Tools from `coop.tribe build` or `settool` (the held type only) are not taken.
* ✘ The social order: CitizenAction 9 (what a social click on another tribe's hut gives, DoActionGeneric 0xCD0C20 case
  0xE: TribeMission_SetTargetTribe, then DoActionForAllSelected(9, the tribe)) on the NPC tribe, from the host (`order`,
  and selected + `act_obj`) and from the guest (`act_obj`: its own DoActionForObjects, sent to the host, which takes
  it: rec=9 with the tribe), with and without the chief, gives the gift group node 0x0462855E (tick 0xDBB7D0: walk to
  the group spot, carry food bundles, ApplyRelationship 0x530CF06) for ~15 s and then idle nodes; no member ever
  entered the performance 0xB46E8803 (`coop.tribe duet`: no performer), so duet_tick never had both owners on one
  target. Labs 20261108-151207, -151615, -152143, -153107, -153517.
* ✔ (Ghidra) The performance's session is made in the enter of BT node 0xF473F46A (0xDA59A0, the "Social" group of
  0x18EB4B7, session index into ai+0x600 +0x1A8, then stimulus 0x200000 and, for the members in cTribeUI +0x94..+0x98,
  stimuli 0x100000 / 0x10000); 0xF473F46A appears only in the BT action table (0x15933EC), its chooser is in the BT
  data. Which order or UI step enters it was not found.

## Fix
Not done (no test yet). On the way (fix/w46-b1-tribe): the twin switch `coop.fix tribe.duet off` (duet_tick does
nothing), `coop.tribe duet` (the performers, their session target, state and the mini-game's progress, ticks=) and
`coop.tribe act_obj <action> <noun> <x y z>` (the selected members' order on an object, as a click gives).

## Verification
Open: no test. Next: find what enters node 0xF473F46A (a social click in social mode with the instruments' members
selected in the host's own UI, by real clicks: play_tribe's Game.social, the HUD's instrument buttons), then the test
of the recipe with `coop.tribe duet` progress, twin `SPORE_FIX_OFF=tribe.duet`.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
- 2026-11-08 (fix/w46-b1-tribe): instruments taken by both players' members (buy + grab); the social order (CitizenAction 9) gives the gift node, never the performance: no test yet; tribe.duet switch, coop.tribe duet / act_obj added.
