---
id: BUG-1139
title: Tests: space_trade fails warm after space_rivalry (the co-op window is still on the Space page, the test waits for the lobby's Space button)
status: fixed
kind: test
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-space2
commits: 49165803
tests: space_trade (warm after space_rivalry), space_rivalry
signature:
docs: docs/06-testing.md#testing-warm-ui-page, docs/archive/known-issues-2026.md#issue-space-trade-warm-page
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
Observed (fix/w51-space2, code): `coop.uistate 0` (the warm reset, BUG-0898) only closes the window (`coop_ui::open(false)`);
`g_space_page` / `g_settings_page` are cleared only by first_start, open_lobby and the page buttons, so a closed window
keeps its page for the next F9 (by design for players).

## Fix
The warm reset puts the window in the cold-start state instead of each test coping: `coop.uistate reset` (new,
coop_cmds.cpp + coop_ui::test_reset) closes the window and puts it back on the lobby page; twoinst.warm_reset sends it
to both games in place of `coop.uistate 0` (an older DLL reads `reset` as 0: closed only). A test-tooling change: no
switch, players' F9 keeps its page as before.

## Verification
- FAIL warm after space_rivalry (before the fix): feat/w51-space runs 20261115-023819, 20261115-030136 (lane 6).
- PASS warm after space_rivalry (`lab.py run space_rivalry,space_trade --in-order`): out/lab/20261115-045007 (lane 6),
  out/lab/20261115-045807 (lane 7): space_trade "warm reset 16 s" right after space_rivalry, PASS.
- PASS cold: out/lab/20261115-042847 space_trade#1 (cold), and space_trade#2 warm after it.
- Not verified: a warm test after one that left the settings page open (the same reset clears it).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-space2 (`coop.uistate reset` in the warm reset).
- 2026-11-18: released in 0.7.15.
