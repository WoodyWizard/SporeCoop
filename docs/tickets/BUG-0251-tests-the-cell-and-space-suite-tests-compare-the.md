---
id: BUG-0251
title: Tests: the cell and space suite tests compare the guest's received network position, not where the guest's game shows the host
status: fixed
kind: test
stage: All
reported: 2026-10-28
source: GitHub #185 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-cell
commits: 1bd70d6
tests: cell, space (coop_mode.py)
signature:
docs: docs/06-testing.md#testing-mode-puppet-pos
related: #185, BUG-0042
---

## Report
From GitHub issue #185: https://github.com/WoodyWizard/SporeCoop/issues/185 (the full analysis, links and the scenario are there).

**Summary**
`coop_mode.py` runs the suite tests `cell`, `tribe`, `civ` and `space`. Its check "the guest shows the host at the new place" reads the position the guest last received over the network, not the position of the host's puppet in the guest's game. In Cell and Space the puppet is a real game object (a cell, a UFO). A guest that receives the host's states but whose game does not take them (puppet writes not applied, a puppet the game moved back, a wrong object) still passes. This is the bookkeeping check BUG-0042 was opened for; it was left in this script (commits 824813c, bb7154e and f7a4b75, release 0.7.3.0). In Tribe and Civ there is no game object to check: the shared "puppet" is a cursor marker inside the mod, so this issue covers Cell and Space only. Severity: low to medium (

## Root cause
Confirmed: `coop_mode.py` compared the host's position with the guest's last network sample (`remote ... pos=`), not with the host's puppet in the guest's game. Seeded failure (lane 2): with the guest's puppets not driven (`coop.puppet hold on`), `coop_mode.py --fixture cell --hold --net-pos` (the old check) printed RESULT: OK while the guest's game still had the host's cell 13 m behind.

## Fix
Cell and Space: `puppet_pos()` reads the `puppet pos=` line right after the host's `remote 0` line; after the move the test polls up to 8 s (host and guest read back to back) until the puppet is within TOL (Cell 1.5 m, Space 8 m) of the host's own position, nearer to it than to the old place, and moved by at least half the host's move. Every sample is printed with the network sample's error next to it. Tribe / Civ keep the old check, printed as a cursor marker (network) check. `--net-pos` keeps the old check; `--hold` seeds the failure with the new test command `coop.puppet hold on|off` (coop.cpp: remote puppets not driven; cleared at session end, never saved).

## Verification
Lane 2: `--fixture cell`: PASS (puppet 0.31 m off after a 6.4 m move); `--fixture cell --hold`: FAIL (puppet 12.8-13.5 m off, moved 0.0 m); `--fixture cell --hold --net-pos`: PASS (the old check misses it). `--fixture space`: PASS (0.50 m off after 4.0 m); `--fixture space --hold`: FAIL (17.5 m off). Tribe / Civ not run (their check is unchanged apart from the printed label).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-cell (1bd70d6): Cell and Space ask the guest's game; seeded failure shown for both.
