---
id: BUG-9218
title: Co-op window: the colour button covers the first letter of the own name
status: fixed
kind: bug
stage: menu
reported: 2026-11-15
source: merge stage tour 2026-11-15
role: both
versions: merge-0.7.14
fixed_in: 0.7.14.1-lineages56-dev
branch: feat/lineages-merge-0.7.14
commits: 4a024f91 9d7f235e
tests: manual Windows QA: the MSI, two games (host MSICivHost, guest MSIGuest), Abber.spo
signature:
docs:
related:
---

## Report
In the merge's stage tour the Co-op window's own colour button (upstream F2) covered the first letter of the player's own name in the player list.

## Root cause
Observed: the button's pill is drawn a few pixels wider than its 26 px area; the name label started right at its edge (x 69).

## Fix
- Names start at x 75 and the stage column at x 199 (a 14-letter "Name (host)" stays clear of it) (mods/coop/coop_ui.cpp, switch ui.color_gap).

## Verification
FAIL (merge build, TITAN tour shot): the colour button over the name's first letter.
PASS (MSI, host and guest windows, 56-dev): names clear of the button, "MSIHost (host)" clear of the stage column.
no-fail-run: Windows QA by hand (no lab on this PC).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed for 56-dev, checked on the MSI (TITAN's test copy did not start after a Steam client update).
- 2026-11-15: released in 0.7.14.1-lineages56-dev.
