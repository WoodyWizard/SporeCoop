---
id: BUG-0565
title: Tribe: every village's relation is neutral on the guest, while the host sees them aggressive and slightly aggressive
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-world-state
commits: a69ee62e
tests: tribe_rel (coop_wsync_tribe.py rel), wsync_transitions (coop_wsync_transitions.py)
signature:
docs:
related:
---

## Report
The guest: all villages are neutral to it; for the host they are aggressive and slightly aggressive.
Reports: SporeCoop-report-2026-11-04 190729 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (code): nothing in the mod sends or compares tribe relations (cRelationshipManager values by
  political id; docs/re/tribe.md B): each game keeps its own; the host's raids and gifts change the host's only.
* Inferred: the guest's game never gets the host's raids' relation events, so its villages stay neutral (W1).

* Observed (lab, 20261104-205053, the fix off): a village the host's game made hostile (ApplyRelationship -7) stays
  0.00 in the guest's game (CalculateRelationship) for 20 s and longer: nothing in a guest's game moves it.

## Fix
The host-owned world state rows (mods/coop/wsync.cpp, docs/sync-architecture-plan.md#sync-wsync): row tribe_rel,
how every NPC village feels about the player tribe (CalculateRelationship, per political id with the village's
position); a guest whose village differs by 0.05 or more gets the host's value through the game's
ApplyRelationship (event 0x530CF07, delta to CalculateRelationshipAbsolute), so its tribe UI follows. Re-applied after
every transition. Switch `wsync.tribe_rel`. Creature species relations (social.cpp MSG_SPECIES) and Space empire
relations (space_diplomacy.cpp) already follow the host's table (not changed).

## Verification
Not reproduced in the lab (the `call` console command returns eax only, CalculateRelationship returns a float in ST0: a relation read needs a command). Package W1.

Lab: tribe_rel FAIL with `SPORE_FIX_OFF=wsync.tribe_rel` out/lab/20261104-205053 (host -7, guest 0), PASS
out/lab/20261104-204941 (guest -7 within 1 s, 10 s later too). Class: wsync_transitions FAIL out/lab/20261104-205648
(host -7, guest 0 before and after the game over; -3 after the guest's own change), PASS out/lab/20261104-205651.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (W1), observed in code, not reproduced at runtime.
- 2026-11-04 (fix/w43-world-state, W1): fixed; see Fix and Verification.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:state:relation` in 34 % of the Tribe comparisons of long_tribe / play_tribe on rel/0.7.7 (0.7.6 code), labs 20261104-203017 and 20261104-210208: e.g. the tribe at (446 -150 185) -7.00 (aggressive) on the host, 0.00 (neutral) on the guest, the report's picture.
- 2026-11-05: released in 0.7.7.
