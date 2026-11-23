---
id: BUG-1453
title: defer_identity: the 1500-frame reuse hunt (BUG-1406) spends the round budget: its late frees were the cinematic's end, not a time effect; 16 rounds gave the fix-off twin 0 reuses twice
status: open
kind: test
stage: Creature
reported: 2026-11-23
source: lab
role: 
versions: 
fixed_in:
branch: fix/w65-tests
commits: 15a93727
tests: tools/tests/coop_defer_identity.py (defer_identity)
signature:
docs:
related: BUG-1406, BUG-1099, BUG-0825
---

## Report
Night script of the fourth 0.7.16 final check, out/night-0.7.16-d/1-fixoff-defer.txt (lab 20261122-192135):
`SPORE_LAB_FIXOFF="defer_identity=npc.defer_identity" lab.py run defer_identity --repeat 2`: 2 INCONCLUSIVE "no block
reuse in the time".

## Root cause
Test (its hunt), plus luck; not the switch. The fix-off was applied; npc.defer_identity changes nothing the hunt needs
(the queue entry and g_puppets are plain pointer vectors; destroy_deferred_tick unchanged since 0.7.13, whose fix-off
twins reproduced 6 times). Per round (16 + 16, all used, ~26 s a round, 23 s of it the 1500-frame hunt): #1 12 rounds
refs_now=1 freed_at=-1 for all 1500 frames, the block freed 5-8 frames after the hunt's end read (frames 2197->2203,
3886->3892, ...: just after the `key enter` that ended the cinematic), 2 rounds refs=3 never freed, 2 false frees
(header 16A9 -> 16A8: the chunk before changed); #2 9 freed at the cinematic's end, 3 refs=3, 1 false free, 3 merged
backward (M). Over all logged rounds since 0.7.14 (116 fix-on + these 32) a true in-hunt free comes 9-19 frames after
the destroy (11 of 100 watched fix-on rounds, all 8 reuses); every later "free" was the cinematic's end. So BUG-1406's
"late frees at 1026-1295 frames" (and its 1500-frame hunt) were a misreading: the long hunt only spends the round
budget (16 rounds; fix-on runs too were INCONCLUSIVE 4 of 12). Two fix-off INCONCLUSIVE at ~7 % a round: luck.

## Fix
coop_defer_identity.py: 300-frame hunts (all true in-hunt frees seen came by 19 frames since 0.7.14, by 260 in 0.7.13)
and up to 60 rounds within the same 9 minutes (~8 s a round: P(no reuse) ~1 %). Checks unchanged. Not done (a mod
change, mods/coop/npc.cpp reuse_watch_chunk): a header change in bit 0 alone means the chunk before changed, not that
the target was freed (3 of 32 twin rounds, ~15 fix-on rounds counted as freed) - for a branch fix/w65-mod.

## Verification
Open, waiting for the lab run: PASS `SPORE_DETECTOR=20 python3 tools/lab.py run defer_identity --repeat 2 --lane N
--retry 0 --no-build`; FAIL `SPORE_LAB_FIXOFF="defer_identity=npc.defer_identity" SPORE_DETECTOR=20 python3
tools/lab.py run defer_identity --repeat 2 --lane M --retry 0 --no-build` ("that creature still lives ..."). If the
twin's ~60 rounds show no in-hunt free while fix-on runs do, the switch matters after all (then RE: who holds refs).
no-fail-run: a test change; INCONCLUSIVE before 20261122-192135.

## History
- 2026-11-23: opened; fix/w65-tests: short hunts, many rounds.
