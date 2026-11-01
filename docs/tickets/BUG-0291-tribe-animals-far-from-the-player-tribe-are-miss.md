---
id: BUG-0291
title: tribe: animals far from the player tribe are missing on guests (NPCs are sent only within 250 m of the tribe)
status: fixed
kind: desync
stage: Tribe
reported: 2026-10-30
source: fix/w9-tribe while analysing BUG-0288 (static, from docs/re/host-centric.md M5/M6/M12)
role: guest
versions: 
fixed_in: 0.7.3.2
branch: fix/w9-creature
commits: f7eb82e 8c950ac 5cf09ff
tests: tools/tests/coop_tribe_far.py (suite tribe_far)
signature: tribe: no animals on a guest that looks farther than 250 m from the player tribe
docs: docs/re/creature.md#creature-view-centres-tribe, docs/06-testing.md#testing-w9-creature
related: BUG-0280, BUG-0288
---

## Report
Static reading (not verified at runtime), found while analysing BUG-0288 and pointed out by the
host-centric audit (docs/re/host-centric.md on docs/w9-host-centric, items M5, M6, M12):
* npc.cpp host_tick: in the Tribe stage the host streams NPC animals only within kRtsRelevance
  (250 m) of the stage centre (the player tribe), one centre for every guest; the guest's update
  filter removes the guest's own animals. A guest that looks at a spot farther than ~250 m from
  the village sees no animals there, while the host's game may have them.
* The campaign's coverage check (coop_multi.measure) counts the host's NPCs within 90 m of the
  guest's pointer: with the pointer far from the village such NPCs are counted and never sent, a
  coverage desync that is this gap (or a test artefact, if the host's game has no live animals
  there either: the planet activation pass 0xACE4E0 follows the host's camera).
Not seen at runtime yet. The campaign's coverage finding now names the missing keys per kind
(coop_campaign.py, fix/w9-tribe), so the next 4-player campaign shows whether `n` keys go missing.

How to reproduce (proposed): tribe fixture, 2 players, the guest moves its camera 300+ m from the
village near animals of the host's world; compare `coop.sync` n keys within 90 m of the guest's
pointer on both machines.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (2026-10-30, fix/w9-creature, lane 9): with the guest's pointer 332 m from the host's
camera and the game's animals kept alive there (BUG-0280, Tribe), the host's game updated 18 NPCs
within 100 m of the pointer and the guest had a proxy of none of them: npc.cpp host_tick used one
centre for every Tribe guest (the player tribe, kRtsRelevance 250 m). Two layers: without the
BUG-0280 Tribe spheres the host's game has no animals there either (the coverage check counting
them would be a test artefact); with them the send radius is the gap.

## Fix
npc.cpp host_tick (`npc.rts_views`): in the Tribe stage a guest also gets the NPCs within kRelevance
(110 m, 20 m hysteresis) of its own view centre (`views::of`, its pointer as the host shows it), next
to the 250 m around the tribe. The 250 m around the tribe is unchanged.

## Verification
`coop_tribe_far.py`, its BUG-0291 check (the host's NPCs within 100 m of the guest's pointer have
proxies on the guest):
* FAIL without it (`SPORE_FIX_OFF=npc.rts_views`): out/lab/20261030-013817 (18 host NPCs there, 0
  proxies; the BUG-0280 check passed in that run).
* PASS: out/lab/20261030-013436, out/lab/20261030-014003 (18 of 18).

## History
- 2026-10-30: opened.
- 2026-10-30: reproduced and fixed on fix/w9-creature (npc.rts_views), with BUG-0280 in Tribe.
- 2026-11-01: released in 0.7.3.2.
