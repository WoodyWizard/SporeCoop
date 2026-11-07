---
id: BUG-0678
title: Space: when a player joins again, its own items are gone and it has the host's items instead
status: open
kind: bug
stage: space
reported: 2026-11-07
source: player report 2026-11-07 (Discord bug-reports post 'Items are not saved for players.', tag Space, text only, version not given)
role: 
versions: unknown (0.7.9 is current)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Player report (Discord #bug-reports, new post, tag Space, 2026-11-07), verbatim:
"When players log back in, their items disappear and the host's items appear - I assume this is due to item
synchronization being disabled."
No logs, no version. Reading: in Space, a guest that leaves and joins again (or reloads) gets the host's world save;
its own inventory (cargo, collectables, tools, maybe spice) is replaced by the host's.
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
