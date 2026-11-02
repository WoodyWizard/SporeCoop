---
id: BUG-0420
title: class: hits, events and world state applied without a relation / partner / world filter (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Fixed per case: BUG-0374 (Creature pack), BUG-0131, BUG-0146, BUG-0231 (hand allow-list).
Siblings not covered: Space MSG_UFO_HIT applied on any NPC ship (squadron.cpp:587-601) and NPC hits on puppets forwarded for any attacker incl. host escorts (squadron.cpp:154-167) (obs); Civ joint siege counts NPC nations (0xFE) as a player, captures 1.5x faster (civ.cpp:662) (obs); MSG_VOTE not world-gated: a guest in another world gets the host's evolution editor (coop.cpp:831-839) (obs); host's social table enforced on a guest's own world every 0.5 s (social.cpp:591) (obs); shared Space money into a guest's own Space save (Space not gated in host_world.h) (inf); input validation and world gate are hand lists, new messages open by default (BUG-0201).
General fix: relation_allows(attacker, victim) in every hit path; is_playing_partner(id, in_my_world); world-state messages default-deny at dispatch, tagged where defined. Outcome check: monitor posse/ally rule in every stage; unit test every MSG_ classified.
Source: docs/generalisation-plan.md G3.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
