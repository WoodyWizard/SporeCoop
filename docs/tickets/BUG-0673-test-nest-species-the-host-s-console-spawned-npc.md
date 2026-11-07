---
id: BUG-0673
title: Test nest_species: the host's console-spawned NPC of its own creation is not in its NPC list
status: fixed
kind: test
stage: Creature
reported: 2026-11-07
source: regression run of fix/w46-tribe-civ, 2026-11-07
role: 
versions: 0.7.10
fixed_in: unreleased
branch: fix/w46-misc
commits:
tests: nest_species
signature:
docs: docs/06-testing.md#testing-console-npc
related: BUG-0017
---

## Report
nest_species fails at its third check on lanes 4 and 5, 4 of 4 runs on fix/w46-tribe-civ (5bd355ed, on rel/0.7.10 94066205): `coop.npc spawn 290229B9` on the host answers `spawned <ptr>` and the host's log counts one NPC more (npcs simulated 8 -> 9), but `coop.npc list` on the host has no entry of species 290229B9 within 10 s ("the host mirrors it (None)"). Runs: out/lab/20261107-094633 (lane 5), out/lab/20261107-100325 (2 runs, lane 4), and out/lab/20261107-100618 with every switch of fix/w46-tribe-civ off (creations.png, creations.host_designs, wsync.tribe_level, wsync.tribe_color): FAIL too, so not caused by that branch. Last passing runs in flaky.json are from before 0.7.9.

## Root cause
Observed (code + lab): the test tool, not the NPC registration. `coop.npc spawn` builds the creature in a herd of its
own without an archetype, so it has no tuning (+0xE84) and matches npc.cpp saved_stray (BUG-0385, `npc.stray_ids`,
0.7.8): host_tick gives such a creature no NPC id (the saved partner's puppet case). Real NPCs come from game herds with
tuning, so the world's NPCs are not affected. creature_desync and old_species spawn the same way.

## Fix
npc.cpp: the creatures `coop.npc spawn` makes are remembered (cleared with the world) and exempt from saved_stray and
remove_saved_strays. Switch (tests): `coop.fix tools.console_npc off` (the 0.7.9 behaviour).

## Verification
nest_species: FAIL with SPORE_FIX_OFF=tools.console_npc: out/lab/20261107-113816 ("the host mirrors it (None)");
PASS: out/lab/20261107-113618.

## History
- 2026-11-07: opened.
- 2026-11-07: cause found in fix/w46-misc (the console NPC looked like a saved stray to npc.stray_ids); test tool fixed (tools.console_npc).
