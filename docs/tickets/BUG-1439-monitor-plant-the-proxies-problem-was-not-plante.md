---
id: BUG-1439
title: monitor_plant: the proxies problem was not planted: the guest's coop.npc list read in its 1 s proxy settle window after the warm reset had no spawned proxy, the plant was skipped silently
status: fixed
kind: test
stage: Creature
reported: 2026-11-22
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w65-night
commits: b32a56c9
tests: tools/tests/coop_monitor_plant.py (monitor_plant)
signature:
docs:
related: BUG-0842, BUG-0281, BUG-0399
---

## Report
Fourth final check of 0.7.16 (nightly lab 20261122-194210, lane 4, warm, rel/0.7.16 5376d388): monitor_plant "FAIL the
monitor caught the planted proxies problem"; the other 4 planted problems were caught ("caught after 151 s: ['drawn',
'puppet_hp', 'unlocks']", runs 2 and 3: all caught after 86 s).

## Root cause
Test: the proxies problem was never planted. The test reads the guest's `coop.npc list` once and walks the first
`spawned` proxy away (`coop.npc pwalk`), inside `if nl:` without a word when the list has none. The log has no
`pwalk:` line; the guest's trace has `proxy_under=0 proxy_stuck=0` on every line of the test, no "off the host's NPC".
The guest makes its proxies only 1 s after its avatar or world changed (npc.cpp `g_settle_s = 1.0`, BUG-0842). After
the warm reset the guest reloaded the world: `22:26:00.308 stage: Loading -> Creature`; the plant's first command on
the host was `22:26:01.252 coop.fix parts.shared off` and the list was read ~0.1 s later, at the edge of the window;
`22:26:01.096 npcs mirrored: 8 within 60 m` counts buffered states, not spawned proxies. Passing run 3
(20261121-003202): 1.13 s between the guest's stage entry and the plant, `pwalk: proxy 208 ...`, the rule fired.
Not 0.7.16: 21025f40..5376d388 changes nothing of proxy spawning, the settle window or the monitor's proxies rule.

## Fix
coop_monitor_plant.py: wait up to 20 s for a spawned proxy in the guest's list (a state, not a longer sleep), a check
that there is one and a check that the pwalk reply says it walks ("walks to"), so the plant can no longer be skipped
silently. The monitor's rule and the "caught" checks are unchanged.

## Verification
Was waiting for the lab run: `SPORE_DETECTOR=20 python3 tools/lab.py run monitor_plant --lane N --retry 0 --no-build`
PASS (its log has "ok the guest has a spawned NPC proxy" and a `pwalk: proxy N ... walks to` line).
no-fail-run: a test fix; the FAIL before is 20261122-194210 monitor_plant (the race happens only when the warm reset's
reload ends within ~1 s of the plant).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): monitor_plant PASS in 20261123-015558 and 20261123-023643. Closed.

## History
- 2026-11-22: opened.
- 2026-11-22: fix/w65-night: cause from the guest's stage entry and the plant's time; the test waits for a spawned proxy.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
