---
id: BUG-1443
title: creature_air: the guest is never healed (coop.social has no hp: it only turned the overlay on); bites knocked it out before its later NPCs were tried
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-night
commits: 2811d2c2
tests: tools/tests/coop_creature_air.py (creature_air)
signature:
docs:
related: BUG-0601, BUG-0555, BUG-0437
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 5, warm): creature_air "FAIL on the ground an NPC bites
the guest (#444: 0 hits from the host in 8 s; ['#454 bit on the ground, walked off in the air', '#453 ...', '#452 no
bite', '#451 ...', '#449 ...', '#448 ...', '#446 no bite', '#444 no bite'])".

## Root cause
Test (since it was written, c88909f7): the ground phase "heals" the guest every second with `coop.social hp 10`, but
`coop.social` has no `hp` (coop_cmds.cpp c_social falls through to its last line, which turns the social overlay on);
the setter is `coop.team hp N`. Every biting NPC took health for good: guest trace hp 8/10 at s20, 6/10 s90, 4/10 s120,
2/10 s130, `00:26:47.143 [s 159.998] ko: own avatar dead / knocked out (hp 0/10, Creature)`, then
`busy=cinematic,input_lock` to the end: #448's air rounds ran on a knocked-out guest, #446 and #444 had no bite. The
earlier NPCs walked off as BUG-0601 describes (the game's own flight). No load (fps 64-67, hitches 0). Passing runs
drain the same (20261121-003202: hp 8/10) but stop after one NPC that strikes at once. Not a 0.7.16 regression.

## Fix
coop_creature_air.py: `coop.team hp 10` for each NPC, every second of its ground phase and every 5 s of its air rounds
(the bites are counted from the damage log, the heal hides none). The checks are unchanged.

## Verification
Open, waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run creature_air --lane N --retry 0 --no-build`
PASS, the guest's trace without a knockout. no-fail-run: a test fix; the FAIL before is 20261122-194210 creature_air.

## History
- 2026-11-23: opened; fix/w65-night: the heal goes through coop.team hp.
