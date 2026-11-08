---
id: BUG-0744
title: Tribe: a guest's gather order on a fruit (cFruit 0x2C9CC91) the host's game no longer has within 8 m is rejected (a fruit on the guest only, after BUG-0472/0497)
status: open
kind: desync
stage: Tribe
reported: 2026-11-08
source: G4 analysis: play_tribe w46-k4-bodies out/lab/20261108-020506-2
role: guest
versions: 
fixed_in:
branch:
commits:
tests: play_tribe
signature:
docs:
related: BUG-0472, BUG-0497, BUG-0531
---

## Report
play_tribe in fix/w46-k4-bodies out/lab/20261108-020506-2 failed "the host rejected no guest order (1: ... action=2
target=2/0/2C9CC91 ... rejected: no such target here)": the guest's bot right-clicked a fruit (noun 0x2C9CC91, cFruit) in
its game; the host's game had no such fruit within 8 m of that point. Seen once in 62 play_tribe runs of 2026-11-06..08.
Reproduce: play_tribe --repeat N; the guest's log names the order, the host's the reject.

## Root cause
Not analysed (found by G4 while judging the play_tribe flakes). Inferred: a fruit that exists on the guest only, the
class of BUG-0472 / BUG-0497 (both fixed); the reject itself is the mod's right answer to an order whose target the host
lacks. Possible fixes: a gather order on a fruit the host lacks picks the nearest fruit tree within 8 m, or the guest
sends the tree (target kind 3).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
