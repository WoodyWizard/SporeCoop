---
id: BUG-0591
title: Tribe: in play_tribe the wsync rows tribe_rel and tribe_food stop reaching some villages on the guest (detector rows from about 400 s on)
status: open
kind: desync
stage: Tribe
reported: 2026-11-05
source: lab (fix/w43-crashes) play_tribe with SPORE_DETECTOR=20, 20261105-003630
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0580, BUG-0565
---

## Report
Found by the lab (no player report). play_tribe with `SPORE_DETECTOR=20` on fix/w43-crashes (W1 rows tribe_rel and
the new tribe_food), out/lab/20261105-003630: tribe_npc_food passes (lab 20261105-000745), but in play_tribe from
about 400 s on the detector keeps `Tribe:state:relation` (the village at (446 -150 185): -7 on the host, 0 on the
guest, 9 comparisons) and `Tribe:state:tribe_food` (the village at (502 -12 67): 150 -> 90 on the host, 0 on the guest;
later (446 -150 185) 110 on the host while the guest's fell 80 -> 49), 28 % of the Tribe comparisons. Both rows stuck
together on the same villages: ~ inferred, the guest's copy of those villages is not matched (other political id and
more than 40 m from the host's position in wsync match()), or the guest's rows were not applied then (`playing`).
Not seen in the baseline play_tribe of rel/0.7.7 (lab rel/0.7.7 20261104-230915: no relation rows, no raid).
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run play_tribe --lane N`, `tools/detector_report.py <run>`; read the
guest's `coop.wsync` (differ / applied) and `coop.wsync dump tribe_rel` on both games when the rows appear.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
