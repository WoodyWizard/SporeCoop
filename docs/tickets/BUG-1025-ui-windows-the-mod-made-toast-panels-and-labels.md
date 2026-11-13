---
id: BUG-1025
title: UI: windows the mod made (toast panels and labels, the rebuilt co-op window, the menu button) keep the mod's reference for good
status: open
kind: bug
stage: UI
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-9
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding SPC-9. Not seen at runtime, found by code reading. Every window the mod creates
keeps one reference of the mod's own that is never released, so windows the mod throws away (each notification
toast and its labels, the old co-op window at a rebuild, the old co-op button of the main menu) are never freed.
A slow memory leak a player cannot see (a few hundred bytes to a few KB per toast, inferred).

## Root cause
Observed in code (main efd2e0ba), confidence high for the references, medium for the size:
* mods/coop/sui.cpp:263 new_button and :343 text_edit take `add_ref(w)` "ours; the parent takes its own" on top of
  the creation reference; sui::release (:205) is never called in mods/coop (grep). The extra reference is deliberate
  (the mod keeps raw Win pointers in g_named / g_cache / g_btn_fn / g_edit_max, coop_ui.cpp:455..:476, :521), so the
  audit's "extra add_ref" is the mod's keep-alive, not a mistake in itself; the bug is that it is not dropped when the
  window is discarded: notify.cpp:63 remove() only calls remove_child, coop_ui.cpp:830 / :261 / :302 / :1768 remove the
  old dialog root / menu button without a release.
* :227 load_layout: the calloc'd UILayout `l` (AddRef'd at :239) leaks when o_LayoutLoad fails (:241). Rare.
* coop_ui.cpp:281 sui::image() returns an owned image reference; std_drawable_with_icon / copy_info (sui.cpp:406,
  :421) take their own slot references (correct) and the caller's reference is never released: one image reference
  per menu button drawable build (negligible).
* sui.cpp:482 add_handler: the UiProc from ui_winproc has 1 reference, the window takes a second; the returned
  pointer is dropped by every caller (coop_ui.cpp:289, :459, :860, :1160..:1176, :1701), so a destroyed window's
  proc (and its HandlerBox, never freed by design for hot reload) stays. Negligible.
Player path: yes (every toast), invisible. Severity: Low.

## Fix idea
On discard: forget the window in the mod's tables, then sui::release it (toasts in notify::remove, the old dialog
root and menu button after remove_child); release the image after std_drawable_with_icon; free `l` on a failed
load; keep add_handler's pointer and remove_handler it with the window. Switch: `core.ui_release off`. Risk: a
release while the game's UI still points at the window (the reason for the extra ref): release only after
remove_child and outside the frame.

## How to test
New coop_notify_leak.py (one instance, menu): 300 toasts (`coop.notify test`), then `kit.memcensus` / the
UTFWin window count (coop.ui tree all | wc); assert the window count returns to the start value after the toasts
expire. FAIL on main (count grows by ~2-3 per toast), PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
