---
id: BUG-0600
title: Tribe: a guest's game plays none of the hit and fight sounds of the members' fights
status: open
kind: bug
stage: Tribe
reported: 2026-11-05
source: lab (fix/w44-tribe-guest, BUG-0568 analysis)
role: guest
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
Found in the lab while analysing BUG-0568 (no player report yet): in a 40 s fight of the host's members (tribe
fixture, `coop.tribe act 7`), the host's game played the fight's hit / weapon sounds 0xA7E3E1FB (18), 0xD05B58ED (17),
0xD967193C (5), 0xCE5F2ED0 (3) (caller chain 0xA3749B, 0xA83B18: effect-attached sounds) and the creature voices
0xC881120E / 0x529FEC69 (0xC1B0CC / 0xC1B170); the guest's game none of them (`coop.audio list`, out/lab/20261105-115203).
Reproduce: two games on the tribe fixture, the host's members attack an NPC member, then `coop.audio list` on both.

## Root cause
Inferred, not analysed: the sounds come with the effects and the hit reactions of the host's members (TakeDamage, the
behaviour trees' weapon effects); a guest's mirrors take no hits of their own and run no tree. ~ The effects relay
(effects.cpp) covers the players' own creatures, not the members. Not fixed (out of the scope of A1, 0.7.8).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
