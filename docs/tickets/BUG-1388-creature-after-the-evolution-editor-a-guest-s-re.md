---
id: BUG-1388
title: Creature: after the evolution editor a guest's remade nest proxies walk after the host's NPCs that the host's game re-placed 5-6 m in one step (campaign_civ_jump proxies gate)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-20
source: lab
role: guest
versions: rel/0.7.16
fixed_in: 0.7.99
branch: rel/0.7.99
commits: c574a479, 46dab65a
tests: npc_creature_replace, npc_creature_replace_off
signature: proxies: guest: 2 NPC proxies over 3 m off the host's NPC (Creature) on 2 lines
docs:
related: BUG-0440, BUG-0503, BUG-0842, BUG-1375
switch: npc.creature_replace_snap
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, campaign_civ_jump, 4 players, lane 2): the lab's monitor gate
`proxies: guest: 2 NPC proxies over 3 m off the host's NPC (Creature) on 2 lines from s 260` on Bob (guest), right
after the Creature -> Tribe vote's own editors. Found by fix/w61-civ (Tribe / Civ) while triaging campaign_civ_jump;
the Creature stage is not that package's area: not fixed there, handed to the Creature package.

## Root cause
Observed (Bob's log): `Editor -> Creature` at 13:41:34.311 (s 254.9); in that frame npc.proxy_identity found the
creatures of proxies 1, 2, 4, 5, 27 (the nest's members, the players' species 065CBE70) replaced by the game while
Bob was in the editor and made them again. At s 259.1 and 260.1: `npc: proxy 2 (species 065CBE70) 6.0 m off the
host's NPC for 2.1 s ... host's speed 0.0 m/s (placed on the host's path); went off at 5.3 m ... the host's NPC's
largest step between two audits (0.25 s) 5.6 m, 0.8 s ago (BUG-0440)`, 7.1 m for 3.1 s; `walk err 7.80 m ... place
lost 76, teleports 2`. Inferred: the host's game also re-placed its nest members after its editor (one step of 5.6 m
while standing: speed 0), and the guest's new proxy walked after it on the host's path instead of being put there,
so it stood 5-7 m off for 2-3 s (2 proxies, 2 monitor lines: the gate). Not seen in the first final check's run of the
same test (20261120-041304).

## Fix
Not fixed on fix/w61-civ (Creature area). Idea for the Creature package: a proxy whose host NPC moved more than ~3 m in
one audit step while its host speed is 0 (a re-placement, not a walk) is put at the host's position (the teleport
path) instead of walked; or the monitor rule's BUG-0440 exemption covers the first seconds after an own editor.

## Verification
PASS: out/lab/20261124-155436 (npc_creature_replace, 0 faults).
FAIL: out/lab/20261124-161419 (npc_creature_replace_off: creature_replaces=0, err 4.16 m).

## History
- 2026-11-20: opened (fix/w61-civ, from the campaign_civ_jump triage); not fixed there: Creature area.
- 2026-11-24: released in 0.7.99.
