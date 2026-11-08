---
id: BUG-0722
title: Creature: the puppet of a guest playing its own species has no archetype tuning (+0xE84) on the other games
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0431 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-a
commits: ad15c36c 97e3c4d8
tests: puppet_tuning
signature:
docs: docs/re/creature.md#creature-puppet-tuning
related: BUG-0431, BUG-0008, BUG-0118, BUG-0462, BUG-0385
---

## Report
From the BUG-0431 audit (K2b, 2026-11-08): game code reads a creature's archetype tuning without a null check (the callers of 0xC0C300: 0xBA43E0, ApplyEvent 0xBA4A00, CreatureSocialSuccess 0xD9BBD0, Create 0xC09B40; the direct read 0xC115E0); guards exist for three readers only (SelectUI, the stage event handler, the AI tick). Inferred fault, no repro. N1 (0.7.11) saw puppets with +0xE84 set in two-player runs.

## Root cause
Observed (runtime, lane 6, four players: Bob with his own profile, Carol and Dave of the host's world): the puppet of Bob's own species (7CB508F6, its herd without an archetype) had +0xE84 = 0 on the host, Carol and Dave for the whole run (lab 20261108-040137, again 20261108-041250 on Carol and Dave), while every other puppet and every proxy had a tuning (proxies: npc.tuning, BUG-0462). The two-player runs of N1 had no guest of its own species. A fault was not reached in these runs: the hunt (two guests' hits on the host, the kill credit, ApplyEvent) and the guests' social acts ran with that puppet without a caught fault; the readers' crash stays inferred (the class of BUG-0008 / 0xD48634).

## Fix
`npc.puppet_tuning` (npc.cpp `puppet_tuning_tick`, both roles, the Creature stage, five times a second): a partner's puppet without a tuning gets the avatar's (the player archetype) through cCreatureBase_SetTuning 0xC0C2E0; `coop.npc` `puppets_set=`. One mechanism for every reader instead of a guard per reader; the existing guards stay for creatures of saves. Off: as before.

## Verification
Test `puppet_tuning` (tools/tests/coop_puppet_tuning.py: four players, a hunt, social acts, a rejoin; every game's creatures sampled for +0xE84 = 0 among ours).
* FAIL with `SPORE_FIX_OFF=npc.puppet_tuning`: out/lab/20261108-041250 (Bob's puppet untuned on Carol and Dave)
* PASS: out/lab/20261108-041635, out/lab/20261108-052622 (none untuned on any game, all 23 + 23 + 23 + 3 of ours tuned, no fault)
* Also out/lab/20261108-040137 (the first run, before the fix: untuned on the host, Carol and Dave).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-070731, PASS out/lab/20261108-054842 and out/lab/20261108-070731-2 (the test now makes the first guest play a creature of its own: the 4-player run without it passed with the switch off, 20261108-070003, as since BUG-0345 a guest plays the world's species).
Second PASS with the new set-up: out/lab/20261108-071104.

## History
- 2026-11-08: opened.
- 2026-11-08: fix/w46-k2b-content-a: reproduced the untuned puppet (a guest of its own species), fixed (npc.puppet_tuning), test puppet_tuning.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
