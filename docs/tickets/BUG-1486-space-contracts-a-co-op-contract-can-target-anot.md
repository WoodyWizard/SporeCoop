---
id: BUG-1486
title: Space contracts: a co-op contract can target another empire's colony (tech level 5 is any colony) (SPC-11)
status: fixed
kind: bug
stage: Space
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w77-space
commits: 7c5e6258, 37d9240d
tests: space_own_mirror, space_own_mirror_off_armada, space_tests (make test)
signature:
docs: docs/re/space.md#space-audit-0717
related: BUG-0191, BUG-0543, BUG-1487
switch: contracts.host_colony
---

## Report
External static audit of 0.7.15.5, item SPC-11 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-civ-spc.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-X1 (fix/w77-space). A behaviour change gets a switch and a twin.

## Root cause
Inferred from code, confidence high: space_contracts.cpp `colony(rec)` was `tech +0x194 == 5`, which is any empire's
colony (docs/re/space.md 5.2: ownership = tech 5 + a cCivData of the empire's political id). armada_planet("")
first takes the host's planet, then colony_of_star of the host's star: with the host in an NPC empire's system the
auto offer (every 600 s) targeted the NPC's colony; StartRaid makes it a raid of the star's owner, so spacecoop's
h_SetState never called the raid listener and the wave ended only by the "ships gone" rule or the time limit. A
partner's own-star mirror colony on the host's home star (BUG-0543, it carries the host's political id) could be
picked too.

## Fix
colony() also needs a cCivData (+0x15C, pol +0x14) of the host's political id (cEmpire +0x84) and refuses a partner's
own-star mirror colony (`spaceext::allies_own_mirror`); the rule is `spacelogic::host_colony` (unit-tested). Switch
`coop.fix contracts.host_colony off`: 0.7.16.1. Status: `contracts` not_host_colony=. Test seams: `coop.space
contracts jump <star>` (BUG-1490), `coop.space allies npccolony`.

## Verification
open, waiting for the lab run.
`coop_space_own_mirror.py` (suite `space_own_mirror`, twin `space_own_mirror_off_armada`): an armada contract against the
guest's colony in the home system and against an NPC empire's colony near the host is refused; one against the host's
own colonies is offered. Expected: PASS with the fix; FAIL with SPORE_FIX_OFF=contracts.host_colony.
pending night run: space_own_mirror space_own_mirror_off_armada (1 lane, about 10 min).
0.7.17 release lab: PASS: space_own_mirror (lab 20261124-020506); FAIL (fix off): space_own_mirror_off_armada (lab 20261124-005839).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-X1), branch fix/w77-space.
- 2026-11-23: fix, lab test and FAIL twin on fix/w77-space (WP-X1); no lab run yet (lab runs only at night).
- 2026-11-24: released in 0.7.17.
