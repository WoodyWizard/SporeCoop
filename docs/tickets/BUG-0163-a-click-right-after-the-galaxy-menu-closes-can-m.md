---
id: BUG-0163
title: a click right after the galaxy menu closes can make a virtual call on the freed "Co-op" menu button
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#142
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: dcce3fb, ad62745
tests: tools/tests/coop_ui_menu_leave.py (suite ui_menu_leave)
signature:
docs:
related: #142
switch: core.menu_drop
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/142 (full text, discussion).

`coop_ui::tick` checks every new left click against the galaxy menu's "Co-op" item. It calls `sui::visible(g_menu_btn)`, a virtual call on the window, before it checks that the menu (and its intro) is still there. `g_menu_btn` is cleared only by the outside-frame refresh, every 1 s while the co-op window is closed. So for up to 1 s after the galaxy menu is gone, a click calls through the vtable of a possibly freed window. Severity: low (short window; a crash if the memory was reused).

### Where
- The check, with `sui::visible` before `world::menu_intro_playing()`: [coop_ui.cpp#L802-L807](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L802-L807).
- `sui::visible` -> `flags(w)` -> `vcall(w, 0x28/4)`: [sui.cpp#L133](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/sui.cpp#L133), [sui.cpp#L150](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/sui.cpp#L150).
- `g_menu_btn` is cleared only in `ensure_menu_button`, run from `refresh` (period 1 s while the window is closed): [coop_ui.cpp#L117-L124](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L117-L124), [coop_ui.cpp#L832-L839](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop_ui.cpp#L832-L839).

### How to reproduce
2 players not needed:
1. Galaxy menu with the "Co-op" item visible and the co-op window closed.
2. Double-click a saved planet's Play (or click Play and click again at once).
3. With page heap or a debug allocator (or Application Verifier under Windows), the second click shows a read of freed memory in `sui::visible` from `coop_ui::tick`. Without one it usually reads stale but intact memory, so it rarely crashes.

### Expected
No access to a menu window after the game left the menu: the item pointer is dropped as soon as the mode is not GalaxyGameEntry, or only used after `menu_intro_playing()` (which checks the mode) is true.

## Root cause
**What happens**

1. In the galaxy menu, `ensure_menu_button` adds the item to the menu window and keeps `g_menu_btn` (a child of that window). The mod builds a new item for every galaxy menu, because the menu window is a new one each time (the comment about the fresh drawable).
2. The player starts a planet (or a co-op load starts it). The game leaves GalaxyGameEntry, and the menu window and its children are released.
3. Until the next `refresh` (up to 1 s, 0.25 s if the co-op window is open), `g_menu_btn` still points at the old item.
4. Any new left click in that time evaluates `lb && !g_lb_down && gw && g_menu_btn && sui::visible(g_menu_btn) && ...`. `visible` reads the object's vtable and calls slot 0x28. If the heap has given that memory to something else, this is a call through garbage. Only the last term, `world::menu_intro_playing()`, would have stopped it, because it is false outside the menu.

### Confidence
PLAUSIBLE. Confirmed by reading; a second reviewer agreed. Not checked: exactly when the game frees the menu window after the mode switch.

## Fix
Fixed on fix/w1-core (dcce3fb, ad62745). `coop_ui::tick` drops `g_menu_btn` / `g_menu_parent`
as soon as the mode is not the galaxy menu, and checks `world::menu_intro_playing()` before any
call on the item. `shutdown()` and `debug_state()` do not touch the item outside the menu.
`coop.uistate` shows `in_menu=0|1`. Old behaviour: `coop.corefix menu_drop 0`.

The review suggested:

- Reorder the condition so `world::menu_intro_playing()` (cheap, mode-checked) comes before any `sui` call on `g_menu_btn`.
- Also set `g_menu_btn = g_menu_parent = nullptr` in `tick()` as soon as `A.mode() != modes::kMenu`.

No behaviour change for players, so a fix switch is not needed.

## Verification
One-game test `coop_ui_menu_leave.py` (lane 2, 2026-10-28): the menu with the item, `coop.load` of the
creature fixture, `coop.uistate` polled as fast as the console answers, clicks on the item's place
after the mode changed.
FAIL with `--without-fix`: the first answer after the mode change showed `menu_button=yes in_menu=0`
(the pointer of the freed menu's item still kept). PASS with the fix, 2 runs: every answer
`menu_button=no`, no faults. A crash from the stale call was not seen (the freed memory was intact).

The planned test was:

`coop_ui_menu_leave.py` (one instance is enough):
- The menu is up and the item exists (`coop.uistate` shows `menu_button=yes`).
- Start a load from the menu, then send 10 left clicks in the next second (`sk click`).
- Assert no crash report and no fault in the log.
- Then assert, from the debug state after the mode change, that `menu_button=no` within one frame.

The last check fails on main (it stays "yes" up to 1 s) and passes with the fix.

### Related
#68 (galaxy map name tags react to clicks in other windows), docs/08-ui.md.

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `menu_nofix2.log` (fix/w1-core, menu_drop 0; the first run `menu_nofix.log` did not reproduce), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261027-192300-fix-w1-core on fix/w1-core (ui_menu_leave ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #142.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: menu item pointer dropped with the menu (coop_ui_menu_leave.py).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: core.menu_drop (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
