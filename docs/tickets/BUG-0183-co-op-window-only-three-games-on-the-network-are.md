---
id: BUG-0183
title: Co-op window: only three games on the network are listed
status: fixed
kind: bug
stage: Menu
reported: 2026-10-24
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: guest
versions: 0.7.2.4 (main) and earlier
fixed_in: unreleased
branch: fix/w3-net
commits: aa6d418, 1e905bb, 423392b
tests: coop_ui_status.py (suite ui_status)
signature:
docs: docs/06-testing.md#testing-net-w3
related: BUG-0039
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* The connect page shows the first three answers of the LAN discovery (`lan0`..`lan2`). On a
  machine running several lab lanes every host answers on 127.0.0.1 (and the LAN address), so the
  own lane's host can be the fourth and is not listed; a click on `lan0` then joined another lane's
  game ("rejected: different SporeApp.exe build", lane 9, 2026-10-24). `coop_ui.py` joins its own
  host by address when it is not listed and never clicks another lane's row. (The discovery also
  queried only ports 47810-47819, lanes 1-5: lane 9's host on 47826 was never found. It queries 20
  ports now, lanes 1-10.) Players with more than three co-op hosts on their
  network see only three: more rows or a filter (same build first) would help.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
The connect page keeps its three rows and gets a "More >" button (`lanmore`) next to the header
when more than three games answer; each click shows the next three (wrapping to the first), the
header says "Games on your network: 4-6 of 8". The discovery list is pruned and capped at 32
(BUG-0039). No "same build first" order: the discovery answer carries no build id (a protocol
change). `coop.lanfake <n> [s]` makes up games for tests. Switch: `coop.corefix lan_pages 0`.

## Verification
`coop_ui_status.py` part 3: six fake games plus the lab hosts, real clicks on "More >", every
fake game must show in a visible row.
- FAIL: `coop_ui_status.py --without-fix` on lane 2 (switches off), logs `out/lab/manual-w3-net/ui_status_nofix.log` and `ui_status_nofix2.log` (2026-10-28): 0 of 6 listed (the first three rows were taken by real hosts and `fake1` was not
  among them on that run; no way to see the others).
- PASS: lab runs `out/lab/20261028-005022` and `out/lab/20261028-011423` (lane 2, both PASS): 6 of 6 over three pages ("1-3 of 8", "4-6 of 8", "7-8 of 8").

## History
- 2026-10-24: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: fix/rel-flaky: the test failed when other lanes' hosts filled the list (18 games, fakes on pages 4-6) and the first clicks were dropped; it now turns every page and repeats a click that did not turn the page. Product code unchanged; duplicate rows per host address: BUG-0210.
