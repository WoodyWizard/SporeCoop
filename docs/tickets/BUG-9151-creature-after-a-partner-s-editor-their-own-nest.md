---
id: BUG-9151
title: creature: after a partner's editor their own nest is rebuilt for the new species and the old one stays
status: open
kind: bug
stage: Creature
reported: 2026-11-12
source: found by code reading 2026-11-12 (personal editor research)
role: all
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + 2 guests 2026-11-13: a partner evolves twice)
signature:
docs: docs/re/creature.md#creature-join-editor-first
related: BUG-9135 BUG-9160 BUG-9161 BUG-9164
switch: creature.nest_follows_evolution
---

## Report
Found by code reading (not run): after a partner leaves the Creature editor with a new species, their own nest (roadmap phase 2, creature.own_nests) may be rebuilt for the new species, the whole world resent to them, and the old nest left behind as an AI nest of the old body; the host's copy of the family keeps the old look.

## Root cause
✔ Confirmed live 2026-11-13 (TITAN host + 2 guests): a joiner with its own nest (seat 3, 7CE29548 at 470 -141 -139)
evolved; the host logged "home nests: seat 3 (Joiner) has its own nest of 7C91D36A at (462 -168 -127)" (made=1), sent
the whole world again (the joiner reloaded, about 6 s), and the old nest stayed with a member of the old body.
Inferred before: a seat's species follows the partner's new key (party.cpp, around 114-118); home_nests then finds no nest of the new species and builds one (home_nests.cpp, around 204-245). To verify in a game before the personal editor (ROADMAP, "a personal editor with the native cocoon") is built: the nest should change species in place.

## Fix
creature.nest_follows_evolution (on). home_nests.cpp host_tick: a seat whose species changed while its nest of the
species it played before is still near the host's home (and its player is here) gets follow_species (outside the frame):
the herd's species (+0xA4, a species manager profile) is set to the new one and every member of that herd (not puppets,
not dead) takes it with Simulator::cCreatureAnimal::SetSpecies (the member's age kept), as the player's own family does
on its own game (BUG-9161); the party ally and the guest's archived creature (for the save) are updated. No second nest,
no world resend. coop.homes shows `followed=N (M members)` and, for a seat without a nest, why.

## Verification
no-fail-run: manual on TITAN (no lab lane on Windows). FAIL (the code before, 2026-11-13 01:42): the evolution above made
a second nest 30 m away and resent the world. PASS (01:47 and 01:51): "seat 3 (Joiner)'s nest at (462 -168 -127) follows
its evolution 7C91D36A -> 7C6B4CB6 (1 member)", then "7C6B4CB6 -> 7C6CA36E (2 members)"; made=0, world_resent=0, the
nest at the same spot with the new species, the other guest's nest untouched; sync-check OK with both guests.

## History
- 2026-11-12: opened.
