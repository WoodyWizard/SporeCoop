---
id: BUG-1188
title: tribe_duet fails on rel/0.7.15 + BUG-1187: the host's instrument member does not perform after the right-click with its chief in the box
status: fixed
kind: test
stage: Tribe
reported: 2026-11-16
source: lab
role: 
versions: 0.7.15 dev
fixed_in: 0.7.15
branch: fix/w51-duet
commits: e7764e8d, d4c0b6a4, 91d6feaa
tests: tribe_duet
signature:
docs: docs/re/tribe.md#tribe-duet-attack, docs/06-testing.md#testing-tribe-duet-attack
related: BUG-0748, BUG-1187, BUG-1112, BUG-1078
---

## Report
Found by fix/w51-tribefields (BUG-1187) in its Tribe set (lanes 4 / 5, `--quick`, 135 of 136 passed): `tribe_duet` FAILs
"both players' members perform for the same village in the host's game (None)" and "the mini-game's progress rose
with nobody playing (0.00, duet ticks 0)". Only the guest's #2 performs for the NPC tribe (`#2 o1 ... s3 0.00`,
ticks 0); the host's instrument member #3 never shows in the list.
Runs (build of fix/w51-tribefields = rel/0.7.15 5912ab18 + BUG-1187, lane 4): out/lab/20261116-002240 (warm),
out/lab/20261116-020859-2 (cold): FAIL both. v0.7.14.1 on the same lane 4: PASS (fix/w51-tribefields-v07141
out/lab/20261116-021050).
Difference seen: on 0.7.15 the host's box selected [1, 3] (the chief with the instrument member, as the test intends,
BUG-0748 note) and the host's right-click gave CitizenAction 26 with n=2; on v0.7.14.1 the box selected [3] only, no
click gave the order and the test's DoActionForObjects fallback ordered it (1 selected), then both performed. So the
one comparison run is not the same path: not proven to be a 0.7.15 regression; the 2-member order of action 26 may
never start the host's performer (or only on 0.7.15). Unrelated to BUG-1187 (hunger).
To reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_duet --lane 4 --retry 0 --no-build`.

## Root cause
A test problem, not a 0.7.15 regression of the mod (no player-visible change): the host's Drama Manager creature attack
(kind 7) now runs during the test's performance, on the test's own NPC village.
Observed (lanes 4 / 5, the test with diagnostics: `coop.tribe ai` of both performers and their targets, `coop.drama
target`, the game logs):
* the attack's start, in session time, is the same within 1 s in every run of one start-up: 57.1-57.6 s on rel/0.7.12,
  v0.7.14.1, feat/w51-tribe before the A9 merge and rel/0.7.15 with SPORE_START_FIXED=1 (lab 20261116-023030), but
  30.6-31.5 s on rel/0.7.15's lab start (A9, d1fe9727 / BUG-1112; 12 runs, cold and warm). The test's NPC village is
  made at ~12 s and its orders come at ~40-50 s: before, the attack started after the duet had passed.
* its attacked tribe is the test's NPC village (`coop.drama target` = the performers' target in 3 of 3 printed runs;
  SpawnNpcTribe's DramaManager_NoteNpcTribe makes the Nth NPC tribe the event's, docs/re/tribe.md#tribe-attack-event-target).
* in a failing run one performer keeps the order (`rec=26`, target the villager) and stays in the idle node 0x04626360
  (its 8 s wait starting again) for the whole 25 s while the other player's member performs (lab 20261116-022434 #2:
  the host's #3; 20261116-022014 #1 and 20261116-002240: the guest's; 20261116-020859-2: the host's two after its
  2-member click). Which player's member fails varies; the orders reach the host and run (host_command unchanged in
  0.7.15 but for the C1 table, `tribe last order` resolved to the villager).
* rel/0.7.15 without a test change: 2 FAILs in 10 runs (20261116-022014: 1 of 4, 20261116-022434: 1 of 6, the latter with the diagnostics only).
Inferred: the attacked village's members answer the attack (stimulus 0x4000) and a performance for one of them does
not start (vanilla behaviour). The selection difference of the report (chief + #3 with a right-click order vs #3 with
the fallback) is not the cause: both paths passed and failed.
Ruled out: Tribe C1 registry (owner_of / members / host_command compare equal), the tribe.cpp split (pure moves), C3
host busy (no busy during the runs), B1 retired switches (tools/fix_retired.txt has no duet or selection switch;
creature.guest_events' retirement keeps the host path).

## Fix
Test: `coop_tribe_duet.py` holds the host's creature attack (`coop.drama hold 80`, a new test command in drama.cpp:
the Drama Manager's start of these kinds returns as a guest's held event does, until the world changes; no
behaviour for players) and prints the drama state. It also checks more than before: the guest's game shows both
performers playing the host's performance animation (`coop.tribe anims` on both games). Twin `SPORE_DUET_DRAMA=1`:
no hold and the game's own attack started right before the orders (`coop.drama start 7`). No coop.fix switch: no
product behaviour changed.

## Verification
FAIL (rel/0.7.15 test, no change): out/lab/20261116-022014 (lanes 4 / 5, 1 of 4), out/lab/20261116-022434 (diagnostics
only, 1 of 6), and the report's 20261116-002240 / 20261116-020859-2 (fix/w51-tribefields).
FAIL (twin SPORE_DUET_DRAMA=1, the game's attack started before the orders): out/lab/20261116-023931 (2 of 4: neither
member performed, the guest's check fails too).
PASS (the fix, `coop.drama hold 80`): out/lab/20261116-023406 (lanes 4 / 5, 6 of 6, cold and warm; `held:
creature_attack=3` in each, the guest's mirrors played the host's performance animations of both performers).
Related: play_tribe --quick and tribe_duet_off PASS (out/lab/20261116-024350, lanes 4 / 5); make test passed.
Old start (SPORE_START_FIXED=1): out/lab/20261116-023030 (the attack at 57.1-57.2 s again; the test stops earlier, the
guest's first member is its chief, docs/06-testing.md#testing-tribe-duet-attack).

## History
- 2026-11-16: opened.
- 2026-11-16: fix/w51-duet: cause found (the host's creature attack on the test's village, moved into the performance
  by the A9 lab start); the test holds it (`coop.drama hold`) and checks the guest's view of the performance; fixed.
- 2026-11-18: released in 0.7.15.
