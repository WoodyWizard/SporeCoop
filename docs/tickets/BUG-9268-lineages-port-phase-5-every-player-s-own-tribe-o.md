---
id: BUG-9268
title: Lineages port, phase 5: every player's own tribe (own village of their own species, allied, together to Civ)
status: open
kind: feature
stage: Tribe
reported: 2026-11-20
source: 
role: 
versions: 0.7.15.5
fixed_in:
branch: feat/lineages-tribe (fork IltonBJSilva/SporeCoop)
commits:
tests: tools/tests/coop_tribe_seats.py (suite tribe_seats), tests/tribe_seats_tests.cpp
signature:
docs: docs/re/tribe.md#tribe-own-villages docs/01-architecture.md#architecture-own-villages
related: BUG-9260 BUG-9261 BUG-9262 BUG-9271 BUG-9272
---

## Report
The owner's rule (2026-11-20): in the Tribe stage every player has their OWN tribe (own village, members, chief and
food, built from their own creature species); the players' tribes are allied and move to Civilization together when
only they are left (the NPC tribes conquered or allied), with the existing ready vote. 0.7.15.2 has one shared tribe
whose members are dealt between the players. Lineages (PR #196) has it (BUG-9135 there); phase 5 of the port.

## Root cause
Not a bug: a feature to port. Lineages keeps founder villages in a contiguous founder roster and much more around them
(rescues, outfits, stand-ins, the guest's game swapping its player tribe); the port takes the core.

## Fix
tribe_villages.cpp (new), tribe_seats.h (new, pure), founders::Roster by party seat (`<planet>.spo.duofounders`, DUO3),
hooks in tribe.cpp / tribe_host.cpp: the host founds a partner's village of their own creature (SpawnNpcTribe, the
creature archived), allies the players' villages, gives the village's members to its player (nobody's while offline),
splits its own village between the players without one, runs a guest's orders on its own village, refuses attacks on a
player's village; the NPC tribe controller's two count-only GetData callers leave the partners' villages out
(`tribe.founder_npc_budget`); Tribe_IsPlayerOwned and the terrain cursor give a guest the game's own box, click and
right-click for its village (`tribe.guest_native`) and the host's AI treats a partner's members as player members
(`tribe.guest_ai`). The Co-op window shows "Your village: N members, food F". Switch: `coop.fix tribe.own_villages off`
(the shared tribe as before). Symbols: SporeCoop::FormTribe_TribeCountReturn, NpcTribeUpdate_TribeCountReturn,
Tribe_IsPlayerOwned, Simulator::cGameTerrainCursor::GetTerrainCursor (overrides_dc04aee5.sym).

## Verification
no-fail-run: a new feature (on main `coop.tribe villages` is unknown and coop_tribe_seats.py fails at check 1; with
SPORE_FIX_OFF=tribe.own_villages it fails there too). Not run in the Linux lab (no lane there for this batch); run by hand
on Windows lane D (two instances, Steam = the GOG 2024 code), 2026-11-20, agent A's Boheus Creature fixture:
the Creature -> Tribe graduation with the guest on its own creature (the vote, the editor); the host founded the guest's
village 99 m away (species 7CA81684, faction 00002294, relation 8.0) about 20 s into Tribe and sent the world again; the
guest's game showed its own village and owned its 3 members (the same ids as the host's), could not select a host
member; `coop.tribe select` + `walk` and a real box drag + right-click on the guest walked the guest's members 10-24 m
on the host's game; an attack order on the host's chief was refused by the host; the village's food was the same on
both games (20); a leave + rejoin, a save + menu + load and a restart of both games kept the village (none founded
again; loaded with the guest offline from the archive) and its members the guest's; `coop.tribe advance`, the vote, the
City Hall and vehicle editors took both games to Civ together. The first rival formed with the counts left out
(count_views > 0). `make test` passes.

## History
- 2026-11-20: opened; ported on feat/lineages-tribe and run on lane D.
