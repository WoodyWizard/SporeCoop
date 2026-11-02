---
id: BUG-0370
title: Creature: after an editor the host cannot answer a guest asking for the guest's own previous creature (not found here), the NPC is missing on the guest
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player reports 2026-11-01 (host GOG 2024 + Steam guest, 0.7.3.3)
role: guest
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w31-creature-gate
commits: 50bfff7
tests: old_species (tools/tests/coop_old_species.py)
signature: creations: player N asks for species XXXXXXXX: not found here
docs: docs/re/creations.md#creations-old-species, docs/known-issues.md#issue-own-old-species
related: BUG-0278
switch: creations.old_species
---

## Report
Player logs (Creature, right after the host's evolution while the guest had evolved its own creature):
host `creations: player 1 asks for species 7CA388E5: not found here`; guest `npc 142: proxy spawn
failed (unknown species)`, then `sync-check: MISMATCH ... n: 1 missing on the guest (n142)`; OK again
30 s later (after the world share).

## Root cause
Observed (lane 6, 2026-11-01, coop_old_species.py): a player's creature is registered on every
machine under 0x7C000000 | fnv(owner, bundle id) (creations.cpp apply). A new bundle of that player
(its editor, `coop.creations next creature`) registers the new key and drops the old package at
once, on the host and on the guest itself. The host's game keeps creatures of the old species (it
built them before; `coop.npc spawn <old key>` still works after the drop, the species profile is
cached), the NPC sync sends them with the old key, the guest has no package for it, asks
(`creations: species ... unknown here: asked the host`), and the host's `serve_species` finds no record
through the resource manager: "player 1 asks for species 7C329711: not found here" (the players'
signature, run 20261101-094534). The guest's proxy spawn fails every frame until a world share.
Also observed: such a creature, in a herd like a puppet's, is removed by the host's stray pass
(npc.strays) within a second; the players' n142 was not (the host removed one creature of
7CA388E5 as a stray at 13:34:51 and still sent n142 at 13:34:55): its herd had an archetype or
a nest (inferred: a herd member or a creature a save brought back with its herd).
Earlier text (kept): Inferred: 7CA388E5 was the guest's previous creature as registered on the host; after the
guest's own editor (new creature 7CC73097) the host no longer finds the old key, while a leftover of
the old puppet is still in the world (the host removed "a creature of species 7CA388E5 ... from a
save made while a partner's puppet existed" at the same moment).

## Fix
creations.cpp: every player creature applied keeps its (renamed) records by instance (the newest 24);
`serve_species` answers from them when the game no longer finds the key ("species ... is a player's
earlier creature (no longer registered): sent from its kept records"); the guest registers it as a
species of the host's world and the NPC's proxy spawns on the next frame.
Switch: `coop.fix creations.old_species off` (not kept, not answered: 0.7.3.3).

## Verification
FAIL: SPORE_FIX_OFF=creations.old_species old_species, lab 20261101-095328 (lane 6: host "player 1
asks for species 7C329711: not found here", guest "npc 22: proxy spawn failed (unknown species)",
no proxy of the species on the guest); also 20261101-094534 and 20261101-094850.
PASS: old_species 2/2, lab 20261101-095454 (the guest has npc 22 of species 7C329711 in its world,
host old_sent=1); again in the regression run of the branch, lab 20261101-100420.

## History
- 2026-11-01: opened from the 2026-11-01 reports (found while checking the "cubes" mark).
- 2026-11-01: reproduced and fixed (fix/w31-creature-gate, creations.old_species).
- 2026-11-02: released in 0.7.3.4.
