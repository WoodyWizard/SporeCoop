---
id: BUG-1082
title: Tribe: a guest's mirror of an NPC village member 114 m off the host's (sync-check MISMATCH) during the campaign's raids
status: open
kind: bug
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: campaign_tribe_end (lab gate sync-check)
signature:
docs:
related:
---

## Report
fix/w49-camp, campaign_tribe_end lab 20261113-125221 (lane 5, rel/0.7.14 + fix/w49-camp): the campaign itself reached Civ
the players' way (RESULT: OK), the lab failed it on the sync-check gate: at s 1389 (13:15:57, during the Tribe end's raids,
four seconds after the shortcut `coop.tribe spawn 6`) the host and Carol (extra1) disagreed on one NPC village member:
host `#45 2AD674C0 owner=254 hp=102 pos=(-98.9 -557.5 247.5) bt=04625F64`, Carol's mirror `#45 23A57C50 ours=1
pos=(-80.7 -454.7 201.9) bt=FFFFFFFF host_speed=1.2`, 113.9 m apart, on two checks 3 s apart. The other two guests
matched. Seen once in 3 campaign_tribe_end runs of fix/w49-camp.
Reproduce: `lab.py run campaign_tribe_end` and look for "sync-check: MISMATCH ... t: 1 apart" in the game logs.

## Root cause
Not analysed (outside fix/w49-camp's package). Inferred candidates: the raided village's members killed and replaced
while the guest kept the old mirror id, or a member the conquest moved to another village.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
