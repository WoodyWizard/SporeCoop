---
id: BUG-0999
title: Cell: the host's partner tile pass loops over an empty view set (size() - 1 wraps) when its own view box centre is not finite
status: fixed
kind: crash
stage: Cell
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-3
role: host
versions: 
fixed_in: 0.7.14
branch: fix/w49-cell
commits: 23b91609
tests: cell_views_empty
signature:
docs: docs/re/cell.md#cell-loot-alert, docs/06-testing.md#testing-cell-c1-0714
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-3. Not seen at runtime, found by code reading (verified on main efd2e0ba).
On the host in a Cell session with a partner, `h_PopTiles` builds the view set `g_views` from the host's own view
box centre and the partners' centres. When that centre is not finite, the set stays empty, and the partner pass
computes `np = g_views.size() - 1` = SIZE_MAX and reads `g_views.list()[k]` and `g_view_boxes[k]` past the end: a
crash (or garbage boxes written into the game's view boxes) on the host.

## Root cause
Observed in code (mods/coop/cellsync.cpp on main):
* 2626: `view_bad()` (2121) returns false when `cell.nan_guard` is off, and otherwise only refuses values that are
  non-finite or `> 1e7f`; 2698: the host centre is added only when `views::finite_pos` (view_centres.h:61, `< 1e7f`)
  holds, so with the guard on the gap is the exact edge `|cx| == 1e7` (both box edges at 1e7); with the guard off any
  NaN box reaches it.
* 2749: `size_t np = g_views.size() - 1;` with no `empty()` check; 2752: `g_views.list()[k]`, 2768: `g_view_boxes[k]`.
* BUG-0975's `tiles_overflow` (2600, `cell.tile_cap`) runs before and does not cover this: it reads the level and the
  scale, not the centre.
Severity (inferred): low. Players run with the guard on; the guard-off case is `coop.fix cell.nan_guard off` /
`coop.fix all off` in the lab (the FAIL twin of BUG-0299 would crash here instead of showing BUG-0299's symptom).

## Fix idea
`if (g_views.empty()) skip the partner passes` (and the view box loop), or let the partner block run only when the
host centre went in. No switch needed (a pure bound check); if one is wanted: `cell.views_empty`.

## How to test
New tools/tests/coop_cell_view_nan.py (host + guest in Cell): a test command that writes NaN (or 1e7) into the host's
`Simulator::Cell::ViewBoxes` for one frame with `cell.nan_guard off`; main: the host faults in h_PopTiles (the lab
counts the crash); fixed: no fault, the pass skipped. Hard to see FAIL without a crash: the lab's crash check is the
assertion.

## Fix
cellsync.cpp pop_tiles (h_PopTiles): the partner pass is skipped when the view set is empty (the host's own centre
failed views::finite_pos), counted as `views_empty_skips` (`coop.pool tilestat`). BUG-0975's cell.tile_cap check is
unchanged before it. Switch `coop.fix cell.views_empty off` (unsafe: the off side hangs or faults). Test command
`coop.pool viewedge <n>` (host: its view box with the x edges at exactly 1e7 for n passes; reachable with
cell.nan_guard on, view_bad refuses only > 1e7).

## Verification
Test `cell_views_empty`.
FAIL (SPORE_FIX_OFF=cell.views_empty): out/lab/20261113-023139-2 (the host hung in the pass: a hang report, the
console timed out).
PASS: out/lab/20261113-022529-2, out/lab/20261113-023438-2 (30 passes skipped, no fault).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-cell (package C1).
- 2026-11-14: released in 0.7.14.
