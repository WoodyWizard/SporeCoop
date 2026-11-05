---
id: BUG-0593
title: Civ: a city's owner, the civilizations' relations and money differ between the games (detector planet_owner / state:relation rows)
status: open
kind: desync
stage: Civilization
reported: 2026-11-05
source: detector, rel/0.7.7 final check out/lab/20261105-015320 and 015350
role: guest
versions: 0.7.7-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The detector rows added in 0.7.7 (D0) show it in campaign_space (42 samples, 33 % of the Civ comparisons) and
play_civ: "1 of 10 building_city_hall with another owner (at (-300 409 -67): 800021CA (player) on the host,
000021CB on the guest)", the same for industry / entertainment / house buildings; "1 of 4 civilizations differ in
rel (000021CB: -3.21 on the host, 3.00 on the guest)"; "1 of 6 civilizations differ in money (000021D3: 0.00 on the
host, 2000.00 on the guest)"; civ_coop: "money -800 on the host, 0 on the guest"; play_civ / campaign_space:
"village: 11 on the host's planet, 12 on the guest's" (with BUG-0539's hut / tool / food mat rows). Looks like a city
the player took (captured or bought) stays the NPC's on the guest, with the relations and money of that NPC civ.
Reproduce: `SPORE_DETECTOR=20 tools/lab.py run campaign_space` (and play_civ).

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-05: opened.
