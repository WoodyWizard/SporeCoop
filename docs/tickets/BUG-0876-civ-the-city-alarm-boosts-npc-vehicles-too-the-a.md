---
id: BUG-0876
title: Civ: the city alarm boosts NPC vehicles too, the attacking army included
status: open
kind: bug
stage: Civ
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0771, BUG-0773
---

## Report
Found with GitHub #199 (BUG-0771): the +50 % alarm boost is given to every vehicle within 600 m that is not the alarmed city's owner, also to NPC nations' vehicles, so the army that attacks the city drives faster as well (and stacks the same way).

## Root cause
Read in the code, confidence high: `mods/coop/civ.cpp:1880` filters only `h.owner == owner`; NPC vehicles are in `g_hv` with owner 0xFE.

## Fix idea
Skip owner >= 0xFE and players that are not in the session when the boost is handed out.

## How to test
`coop.civ alarm <a player's city>` with NPC vehicles within 600 m, then `coop.civ boosts`: no entry with owner=254 (there are on main); read +0x218 of an NPC vehicle before and after.

## History
- 2026-11-10: opened from the Tribe / Civ / Space code audit for the 0.7.13 plan.
