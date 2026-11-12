---
id: BUG-0748
title: Tribe: no test of the duet (both players' performers on one NPC tribe)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-08
source: BUG-0199 (fix/w46-g4b-coverage, 2026-11-08)
role: 
versions: 0.7.10 and earlier
fixed_in: 0.7.12
branch: fix/w46-b1-tribe
commits: 7e502373, 9e9f8685
tests: tribe_duet
signature:
docs: docs/re/tribe.md#tribe-duet-test
related: BUG-0199
switch: tribe.duet
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
A test gap. Found (fix/w46-b1-tribe, lane 7):
* ✔ The performance is not the social click on a hut (CitizenAction 9 there gives the gift node 0x0462855E, labs
  20261108-151207 .. -153517) but, in social mode with instrument holders selected, a right-click on a **member** of
  the village ("Right click on a tribe member to perform for them", the game's hint): **CitizenAction 26** on that
  member (lab 20261108-161904: `action=26 n=2 obj=... noun=18EB4B7`), and the members go into node 0xB46E8803 within
  2 s with a session on the village (`coop.tribe duet`), the host's HUD shows the social meter and "Instruments allow
  you to respond to crowd requests by pressing the matching buttons".
* ✔ Instruments for both players' members: bought the game's way (`coop.tribe buy` + `buy_end`) and taken by a host
  GrabTool order (`coop.tribe grab`), also for the guest's member.
* ✔ A guest's member ordered with 26 (its DoActionForObjects, sent to the host) performs in the host's game for the
  same session; with the host's performers both owners are on one target and duet_tick adds 0.04 every 0.5 s.
* ~ The host's right-clicks gave an order only with its chief in the box (161904, 163102); with the instrument member
  alone they gave none. The guest's right-clicks on the village's members gave walks (its pick hit the ground).

## Fix
The test tribe_duet (coop_tribe_duet.py). Tools on the way: the twin switch `coop.fix tribe.duet off` (duet_tick does
nothing), `coop.tribe duet` (performers, session target, state, the mini-game's progress, ticks), `coop.tribe act_obj`.
The duet itself was not changed.

## Verification
Test tribe_duet (coop_tribe_duet.py, new, cold, ~2 min): instruments bought and grabbed by the host's and the guest's
member, each player boxes its member (the host's with its chief), social mode, right-click on a member of the NPC
village (a player whose clicks give no order: its game's DoActionForObjects with 26, printed); in the host's game both
owners perform for the same village and the mini-game's progress rises over 20 % with nobody playing.
* FAIL (SPORE_FIX_OFF=tribe.duet): out/lab/20261108-163102-fix-w46-b1-tribe (lane 7): the host's real right-click gave
  26, #1 / #3 (host) and #2 (guest) perform for the same village, progress 0.00 for 25 s, duet ticks 0.
* PASS: out/lab/20261108-163746-fix-w46-b1-tribe #1, #2 (lane 7): progress 0.08 a second (0.56 after 7 s), ticks 14.
  In both the host's box missed its chief and its clicks gave no order: the orders came through DoActionForObjects.
  Also 20261108-163314 #2 PASS (#1 failed in the test's selection, fixed by box retries / SetSelected).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-g4b-coverage): opened as a child of BUG-0199 after an attempt with a run (recipe and blocker above).
- 2026-11-08 (fix/w46-b1-tribe): instruments taken by both players' members (buy + grab); the social order (CitizenAction 9) gives the gift node, never the performance: no test yet; tribe.duet switch, coop.tribe duet / act_obj added.
- 2026-11-08 (fix/w46-b1-tribe): the performance starts with CitizenAction 26 on a village member (social-mode right-click); test tribe_duet written, FAIL with tribe.duet off, 2 PASS.
- 2026-11-11: released in 0.7.12.
