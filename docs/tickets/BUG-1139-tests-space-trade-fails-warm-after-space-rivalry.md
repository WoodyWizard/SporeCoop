---
id: BUG-1139
title: Tests: space_trade fails warm after space_rivalry (the co-op window is still on the Space page, the test waits for the lobby's Space button)
status: open
kind: test
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-1133
---

## Report
`space_trade` FAILS "F9: the host opens the Space page" ("timeout waiting for lobby with the Space button") whenever it
runs warm after `space_rivalry` on the same lane (feat/w51-space runs 20261115-023819 and 20261115-030136, lane 6);
every trade check after it passes, and the same test cold passes (20261115-025652).

## Root cause
Inferred: space_rivalry leaves the co-op window on its Space page (coop_ui.cpp `g_space_page` stays set over the warm
reset); the lobby's "Space >" button is hidden while that page is shown (coop_ui.cpp: `space && !g_space_page`), and
coop_space_trade.py `open_space_page` waits only for the button. coop_space_rivalry.py already accepts "page == space".
Fix options: the warm reset closes the co-op window's pages, or open_space_page accepts the page already open. Not a
mod regression; not fixed in feat/w51-space (outside C5/C6).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
- FAIL warm after space_rivalry: 20261115-023819, 20261115-030136 (feat/w51-space); PASS cold: 20261115-025652.

## History
- 2026-11-15: opened.
