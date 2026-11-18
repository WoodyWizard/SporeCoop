---
id: BUG-9200
title: Adventure enemies pick a partner's body but never strike it: the body has no political id
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-14
source: roadmap
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 1e547391
tests: tools/qa_win (two TITAN games by hand: City Assault, the guest captain among the enemy defenders, 40-45 s with the switch off and on)
signature:
docs: docs/re/space.md#space-adventure-body-side
related: BUG-9114 BUG-9113 BUG-9089
---

## Report
BUG-9114 (roadmap phase 7, decision 3N A): with `adventure.enemies` on, the host's scenario creatures picked a partner's body as their target (npc partner_picks) but never struck it, so a guest was only hurt by buildings.

## Root cause
Observed (memory, City Assault, 2026-11-14): the cGameData political id at object +0x30 (docs/re/civ.md) is 1 for the captain, its army (class 1027) and its characters (1069), 2 for the enemy defenders (1083); the partner body (modes.cpp adventure_spawn) had -1. Setting it to 1 by hand made the host's creatures strike the body at once. Its scenario marker (+0x18) and class (+0x1C = -2) were already the captain's. A field-by-field comparison of soldiers of both armies found no other side field.

## Fix
adventure.body_side (on): while the host runs the creatures (adventure::partners_targetable), each partner body on the host gets its captain's political id every tick; else -1 as before. `coop.avatar` shows "side=<body> (captain <own>)" per partner. adventure.enemies stays off by default: see BUG-9114 for what is still open.

## Verification
Live, two TITAN QA games (2026-11-14, DuoQAAdvHost + DuoQAAdvGuest, City Assault, adventure.enemies on), the guest captain put among the enemy city's defenders (coop.npc gopos 175 285 -384): FAIL (`coop.fix adventure.body_side off` on the host, body side -1): 0 hits in 40 s and 0 in 45 s with 1-3 enemies within 30 m. PASS (on, side 1): 5 hits in 40 s and 8 in 45 s ("1 hit by npc ... forwarded" on the host; the guest's log "received 20.000, native HP ..."; the guest captain 300 -> 220). no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-14: opened.
- 2026-11-14: fixed (the body takes the captain's side), live-tested with two games.
