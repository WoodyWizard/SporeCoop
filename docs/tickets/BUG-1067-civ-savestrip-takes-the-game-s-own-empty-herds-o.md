---
id: BUG-1067
title: Civ: savestrip takes the game's own empty herds out of Civ saves (they look like the herds the mod makes)
status: open
kind: bug
stage: Civ
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch:
commits:
tests: civ_save_strip
signature:
docs:
related: BUG-0973, BUG-0255
---

## Report
Found by fix/w49-civ (BUG-0973) while checking that wild-creature proxies stay out of a Civ guest's save
(civ_save_strip, lane 4 + Carol on another lane, lab 20261113-043251): `coop.savestrip` after Carol's save says
`empty_herds_stripped=77` (and `ours=9` herds made for proxies). In Civ the game's own herds have no archetype (+0x88)
and no nest (docs/re/civ.md#civ-wild-sync), which is savestrip's "own kind" test (BUG-0255: an empty own-kind herd
is stripped), so empty herds of the game itself are probably left out of every Civ save, the host's too.
Reproduce: a Civ world, `coop.save`, then `coop.savestrip` (empty_herds_stripped > 0 without any partner's puppet).

## Root cause
Inferred: savestrip's own_kind_herd cannot tell a Civ game herd from ours. Not checked: what an empty game herd
left out of a save means after the load (the reloaded world ran 10 s without a fault, no null seen; a nest or
migration that refers to it?).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
