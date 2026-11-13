---
id: BUG-1009
title: Civ/Space: a guest's change cut at 0 by the shared money floor is still counted in full, the next host total names a wrong cause (host earned / spent)
status: fixed
kind: bug
stage: Civ,Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) NET-5
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-money
commits: d500f3b0
tests: res_cause_floor
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
shared.cpp, switch `coop.fix shared.cause_clamped` (off: 0.7.13): add_counted() writes the change through
shared_add / the resource's setter (money_clamp) and returns what the game really took (after - before); the host's
`applied`, the guest's cause and the host's own notice use it, for a guest's MSG_RES_ADD and for owed changes paid
later (BUG-0691).

## Verification
`coop_res_cause_floor.py` (suite `res_cause_floor`, fixture tribe, SPORE_NET="delay=300"): shared food 400, the host
spends 150 and the guest 350 at once (the 300 ms link makes the purchases cross); the food is 0 on both and the
guest's game (its toasts) must not say the host earned food.
* FAIL (SPORE_FIX_OFF=shared.cause_clamped): out/lab/20261113-005801, out/lab/20261113-010621 (the final script) ("Alice earned 100 food (shared)" on the guest)
* PASS: out/lab/20261113-010344, out/lab/20261113-010732 (with res_cause, civ_money_short, space_spice_away, space_spice_mirror: all OK)
Tested on the Tribe food (same code path as Civ / Space money, no city income noise).

## History
- 2026-11-13: opened from the 0.7.14 audit (plan/0.7.14), verified by code reading on main efd2e0ba.
- 2026-11-13: fixed on fix/w49-money (d500f3b0): applied / causes count what the floor let through; FAIL/PASS in the lab.
