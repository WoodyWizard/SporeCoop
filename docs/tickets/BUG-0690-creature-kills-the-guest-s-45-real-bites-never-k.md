---
id: BUG-0690
title: creature_kills: the guest's 45 real bites never kill host NPC #20 (mirror at 2-6 hp, no kill on the host); fails on rel/0.7.11 base too
status: open
kind: test
stage: Creature
reported: 2026-11-07
source: fix/w46-p4-creature regression run 20261107-212113
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
creature_kills FAIL on lanes 4 and 5: "the guest killed host NPC #20 (065CB8AB) with real input (45 bites)",
the guest's mirror at 2.0 / 6.0 hp, not dead, the host's kill count 0 -> 0. Seen on fix/w46-p4-creature
(out/lab/20261107-212113, 20261107-215918) and on the rel/0.7.11 base 2577101f (out/lab/20261107-220638): not caused
by this branch. Last pass 2026-11-01 (feat/w33-tests-creature). The same window's play_creature runs show the guest's
bot landing few hits (14 of 47 attacks, BUG-0618 class). Not analysed: whether the guest's bites miss (reach, the
target's flight), or land and are not applied by the host. Reproduce: `tools/lab.py run creature_kills`.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened.
