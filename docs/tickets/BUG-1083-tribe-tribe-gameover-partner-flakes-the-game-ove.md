---
id: BUG-1083
title: Tribe: tribe_gameover_partner flakes: the game-over caption never comes, the host's knocked-out chief has health again (168) when the guest's members are down
status: open
kind: test
stage: Tribe
reported: 2026-11-13
source: fix/w49-hutwait, lab 20261113-140322 lane 6
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0922, BUG-0960
---

## Report
Seen on fix/w49-hutwait (BUG-1081, a change that does not touch the knock-out path), lab 20261113-140322 (lane 6,
cold start): phase 1 knocked the host's members out (chief #1 hp 0, dead=1 for 80 s, no caption: ok). In phase 2
the test's "knocked out on the host" list held only #1, and for the whole 70 s wait the chief read `hp=168.0 dead=0`:
the caption never came (`FAIL the game over came on the host`). The rerun on the same build passed
(20261113-141231). Earlier failures of this test: 20261112-092315, 20261112-093040 (fix/w48-tribe-mirrors, before
BUG-0922/0960). Inferred: the host's knocked-out chief got health back (BUG-0922: a knocked-out chief may stand up
again) before the guest's members were down, or the guest's members (#2, #8) were no longer in the host's list.
Reproduce: `SPORE_DETECTOR=20 python3 tools/lab.py run tribe_gameover_partner --lane N --retry 0` (about 1 in 5).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
