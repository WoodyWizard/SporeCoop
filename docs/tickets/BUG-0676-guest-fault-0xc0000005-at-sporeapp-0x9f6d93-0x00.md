---
id: BUG-0676
title: Guest: fault 0xC0000005 at SporeApp+0x9F6D93 (0x00DF6D93, data 0x2C) while it rejoins a Tribe world over a bad network, then 22939 faults (chaos_tribe_awful)
status: open
kind: crash
stage: Tribe
reported: 2026-11-07
source: fix/w46-k3-bodies lab 20261107-113600 chaos_tribe_awful#1 (lane 7)
role: guest
versions: unreleased (rel/0.7.10 + fix/w46-k3-bodies 1cbce569, with SPORE_FIX_OFF=tribe.mirror_keep,drive.lead_keep)
fixed_in:
branch:
commits:
tests: chaos_tribe_awful
signature: 0xC0000005 at 0x00DF6D93 SporeApp.exe+0x9F6D93 data=0x0000002C (main thread); calls 00927987 00F47234 00D486EB 00453A69 00927987 00DF7C2C 00DE825C 007D86D6 0081530F 007D8D6A
docs:
related:
---

## Report
chaos_tribe_awful#1 of out/lab/20261107-113600 (lane 7, SPORE_NET=awful: 250 ms, 120 ms jitter, 10 % loss; SPORE_FIX_OFF=tribe.mirror_keep,drive.lead_keep, i.e. those two at 0.7.9's behaviour) failed `the games survived the chaos`: the chaos bot made the guest leave (`coop.leave`) and join again at 11:40:36-39; the guest 'did not get back into the host world' (no frames for 16 s while loading a world) and its log counts 22939 caught faults, the first 0xC0000005 at 0x00DF6D93 (SporeApp.exe+0x9F6D93, read of 0x2C: a null object's field) on the main thread. The guest's own log ends with the summary line of the session it left (faults 0) and the settings of the new start. The same test with the fixes on, at the same time on lane 6, passed (out/lab/20261107-113600-2 #1), and on rel/0.7.9 it passed once (lane 9).

How to reproduce: `SPORE_LANE=N python3 tools/tests/coop_chaos.py --fixture tribe --minutes 5.0 --seed 874162993 --churn 1` with SPORE_NET=awful (the replay line of the run).

## Root cause
Not analysed (outside K3). Look up 0x00DF6D93 in data/symbols/overrides_dc04aee5.sym and the triage-crash skill; compare BUG-0672 (a guest fault after a Tribe world load, fix/w46-tribe-load).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
- 2026-11-07: opened by the K3 agent (fix/w46-k3-bodies) from a regression run; not analysed there.
