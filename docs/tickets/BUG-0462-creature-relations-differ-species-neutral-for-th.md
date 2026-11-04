---
id: BUG-0462
title: creature relations differ: species neutral for the host are hostile for the guest; the guest's roar does nothing
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in: 0.7.4
branch: fix/w40-creature-acts
commits: 1cea6d51
tests: creature_relations (tools/tests/coop_creature_relations.py)
signature:
docs: docs/re/creature.md#creature-relations
related: BUG-0418, BUG-0420, BUG-0501
switch: creature.relations, npc.tuning
---

## Report
Host's mark (s 2109): 'creatures which show neutral for host are hostile for guest + ROAR ability is broken for guests and work only host'. Relations (social table) and the guest's social / scare abilities on host NPCs (the host's AI runs them) are not shared. Classes G1/G3 (BUG-0418, BUG-0420).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (lane 9, creature fixture, 2026-11-03, `coop.social rel`: the game's own GetRelationship(avatar, npc) and
CreatureSocialMode(npc) per NPC): right after a join 13 of 21 of the guest's proxies had another relation than the host's
NPCs (run 20261103-151806, the code without the fixes). The species table (MSG_SPECIES) was the same on both. Two causes:
1. GetRelationship 0xBA40E0 takes the species record or the NPC's archetype tuning (+0xE84 +0x390) only when one of the
   two creatures is of the player species (creature vt 0xD0 = 0xC0C430: +0xB20 == PlayerSpecies, the world's = the
   host's species). A guest playing its own creature is another species in its own game: every relation came from the
   diet / size rule 0xBA2900 (4 instead of 3 for 12 herbivores, mode 1 instead of 2).
2. One proxy (species 06805133) was made without tuning: Create gives none in a herd of ours (the species' nest herd
   was not found on the guest yet); its relation fell to 1 (hostile) where the host's NPC had 3 (neutral): the report.
Inferred: on the host the same rule 1 applied to the guests' puppets in the NPCs' AI (npc.cpp ai_target). The roar
half of the report is split off as BUG-0501 (not analysed).

## Fix
1. `creature.relations` (creature_hp.cpp): a hook on vt 0xD0 0xC0C430 answers true also for the species of the own
   avatar and of the partners' puppets (a list refreshed every frame): every player's species is the player species.
2. `npc.tuning` (npc.cpp): the host sends the NPC's tuning key (the dword before the tuning node) after MSG_NPC_SPAWN
   (older SporeCoop ignores it); the guest gives the proxy the same tuning (cSpeciesManager tuning map 0x4E00C0,
   SetTuning 0xC0C2E0) when it has another or none.

## Verification
creature_relations, lane 9: the code without the fixes FAIL out/lab/20261103-151806 (13 of 21 differ);
SPORE_FIX_OFF=creature.relations FAIL out/lab/20261103-153033 (12 of 21); SPORE_FIX_OFF=npc.tuning FAIL
out/lab/20261103-152848 (12 of 21: the proxies without tuning); both on PASS out/lab/20261103-152938.

## History
- 2026-11-02: opened.
- 2026-11-03: relations fixed on fix/w40-creature-acts (creature.relations, npc.tuning); the roar split to BUG-0501.
- 2026-11-03: seen again in player reports 2026-11-03 (0.7.3.4 forum post: the guest is attacked by animals that look indifferent on its screen, its hp goes down without an attack animation; 0.7.3.6 direct message: neutral animals, even allies outside the host's pack, target the guest), fixed in 0.7.4 (unreleased). The missing attack animation on the guest's screen is not verified by this fix.
- 2026-11-03: released in 0.7.4.
