---
id: BUG-0515
title: Tribe: epic creatures ('giants') deal only 8 damage
status: open
kind: bug
stage: Tribe
reported: 2026-11-03
source: player reports 2026-11-04 (Discord, w40 triage)
role: host
versions: 0.7.3.6
fixed_in:
branch:
commits:
tests: 
signature:
docs:
related: BUG-0121
---

## Report
Host's mark (0.7.3.6, report 20261103-203858): 'in tribe stage giants only dmg 8'. The host log has 'an epic creature comes! together: 80 food' 30 min earlier and several 'pincer attack: +50% damage'. Which side's damage is meant (the epic's hits, or the tribe's hits on the epic) is not clear.
Target: 0.7.5 (Tribe).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not analysed. To ask: what deals 8 (a giant / epic hitting a member, or a member hitting the epic), and on which screen. Check the co-op epic's tuning (the mod's spawn) against a vanilla epic.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened.
- 2026-11-03: opened (w40 triage), needs the player's details.
