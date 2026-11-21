---
id: BUG-9316
title: Civ (host): after a guest's world reload the host logs a large shared-money spend from that guest (seen after coop.civ swgrant money writes)
status: open
kind: bug
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, fix/l47-decide, Abber)
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9309, BUG-9310
---

## Report
Live QA 2026-11-21 (fix/l47-decide, TITAN, Abber with a fifth founder nation for the guest, BUG-9309). After each world
reload of the guest (a share after a capture, 20-40 s), the host logged `shared money -61900.0 from player 1` /
`CivGuest spent 61900 money (shared)` (09:44:04) and later -45880 (09:45:20); the shared money fell by that much on both
games. Before them the test had written money into nations directly (`coop.civ swgrant <seat> <type> <money>`, +0x98 of
the host's nation and of seat 4's) and fired global superweapons (seat 4's cost 48000 from its own money).
Reproduce (not tried again): the BUG-9309 newcomer setup, `coop.civ swgrant 4 2 100000`, the guest fires type 2, the
capture brings a world share; watch the host log after the guest's reload.

## Root cause
Not analysed. Possibly a test artefact (swgrant writes money the shared-money baseline never saw), possibly the guest's
reloaded game reporting the difference between its loaded money and the last synced value as a spend.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened.
