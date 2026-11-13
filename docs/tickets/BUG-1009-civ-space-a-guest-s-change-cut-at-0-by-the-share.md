---
id: BUG-1009
title: Civ/Space: a guest's change cut at 0 by the shared money floor is still counted in full, the next host total names a wrong cause (host earned / spent)
status: open
kind: bug
stage: Civ,Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-5
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-shared-cause-floor
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding NET-5. Not seen at runtime, found by code reading (2026-11-13, plan/0.7.14).
The shared money (Civ, Space) is cut at 0 by the floor and at 99 999 999 by money_clamp when written, but the host
adds the guest's full change to `applied` and to the guest's cause. The next total's own-change part
(`v - synced - applied`) is then wrong and the guests' feed names a wrong cause: e.g. 200 in the pool, a guest's
spend of 500 arrives (both spent at once): the pool becomes 0, applied -500, own = +300: "<host> earned 300 (shared)"
on every guest, and the guest is said to have spent 500. Money itself is right; only the feed text is wrong.

## Root cause
Observed in code (main efd2e0ba), confidence high: mods/coop/shared.cpp:483-486 `r.set(shared_add(cur, v, floor))`
then `r.applied += v; r.causes[from] += v;` with the full v; :318-319 the same for owed changes; :344-346 the host's
own share from `applied`; the set clamps through money_clamp (:215-220, s_civ / s_space). net/shared_ledger.h:88-92
floors only.

## Fix idea
Add the change that was really applied: `double before = cur; r.set(...); double after; r.get(after); d = after - before;`
then applied += d and causes[from] += d (same for owed at :318). Switch `shared.cause_clamped`.

## How to test
coop_civ_money_short.py (exists, Civ: guest spending more than the pool) extended: host and guest spend at once
so the guest's spend floors; assert the guest's feed line (the game's notification text) has no "earned" for the
host: FAIL on main, PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
