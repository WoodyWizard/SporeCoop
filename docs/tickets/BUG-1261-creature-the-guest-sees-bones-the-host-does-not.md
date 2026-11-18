---
id: BUG-1261
title: Creature: the guest sees bones the host does not have and cannot click them (players' video, split from BUG-1251)
status: open
kind: desync
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis (split from BUG-1251)
role: guest
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1251
---

## Report
From the players' video (BUG-1251's report): the guest sees bones the host does not have and cannot click them; the
guest digs a fossil where the host sees empty grass.

## Root cause
Not reproduced (fix/w53-advorn, 2026-11-18): the creature fixture has the same 12 interactive ornaments in both games
(from the save: the same serials, places and model keys, `coop.wobj orns`), and a copied dig changes the same fields as
the game's own (+0xA3, the reward's +0x10 / +0x24). Not looked at: the bone pile an eaten body may leave, ornaments of
a planet the guest loaded from an older save. See docs/known-issues.md#issue-creature-bones-guest-only.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened (split from BUG-1251 on fix/w53-advorn; that ticket fixed the dug-up model).
