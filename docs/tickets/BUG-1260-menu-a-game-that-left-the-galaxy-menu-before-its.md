---
id: BUG-1260
title: Menu: a game that left the galaxy menu before its start-up video ended shows no main menu panel when it comes back to the galaxy menu
status: fixed
kind: bug
stage: Menu
reported: 2026-11-18
source: fix/w53-advorn: found while testing BUG-1254
role: both
versions: <=0.7.15
fixed_in: 0.7.15.2
branch: fix/w53-menuintro
commits: c291dc58 89f9e9f8 da0280d1
tests: menu_panel, adventure_tutorial
signature:
docs: docs/re/space.md#space-menu-intro-state, docs/06-testing.md#testing-menu-panel
related: BUG-0626, BUG-1218, BUG-1254
---

## Report
Seen in the lab (lane 9, creature fixture, 2026-11-18): both games loaded the fixture before the galaxy menu's start-up
video ended (the lab's quick load; a real guest that joins from its menu at once does the same). After `coop.menu` (the
game's own quit) the galaxy menu came back without its main menu panel: no Galactic Adventures, Sporepedia or Co-op
entries, only the galaxy, the Spore button (Options) and Register (frames of lab 20261118-122432 adventure_tutorial,
out/lane9 menu1.png). Escape, mouse moves to the left edge and clicks on the galaxy did not bring it back; it stayed for
minutes. How to reproduce: start('creature'), `coop.menu` on the host, wait: `coop.ui find "Galactic Adventures"` lists
the entry with its parent panel 0x0615A5FF not visible (fl=0x2).

## Root cause
* Observed (log): "world: the galaxy menu intro would start outside the menu (mode 01654C01): not started (BUG-0626)":
  the video's end message 0x49731AD came while the game was already in the stage, the menu's GGE_intro was refused.
* Observed (runtime): `coop.menuintro` (tests) sends the video's end message 0x49731AD in the galaxy menu as StopMovie
  0xFD79C0 does (MessageSend(0x49731AD, null, 0)): the menu plays its intro and the panel is shown 10 s later.
* ✔ (Ghidra, GOG 2024; docs/re/space.md#space-menu-intro-state) GalaxyGameEntry_IntroMessages 0xDEA820 keeps the intro
  state at [0x16A1344] +0x1EC: the video's end 0x49731AD sets 5 and starts GGE_intro; only the intro's end 0x44F1189 or
  the skip 0x66F6B6F set 6. The menu's update 0xDEAF70 goes on (DBCheck, the main menu) only at state 6.
* ✔ (runtime, lab 20261118-135136 with the fix off) the host's video ended in the stage: the handler set 5, BUG-0626
  refused the intro there, so nothing could ever end it: back in the galaxy menu the state was still 5 and the panel
  0x0615A5FF hidden. The guest of the same run (joined after its video) had 6 and its panel.
* So the cause is BUG-0626's refusal (0.7.9): before it the intro played in the stage and menu_intro_tick ended it with
  EndCinematic (0x44F1189: state 6). The video's end message itself is not refused; the refused part is the intro it
  starts, and the intro's end is what the menu waits for.
* Players (inferred): a player who opens the co-op window with F9 while the start-up video plays (~16 s) and joins, or
  any load in that time; the panel with its Co-op entry is not up before state 6, so the normal path waits for it.

## Fix
`world.menu_intro_return` (world.cpp, c291dc58): in the galaxy menu, intro state 5 with no cinematic and no video
playing for 1 s gets the game's own skip message 0x66F6B6F (MessagePost outside the frame, as the game posts it): state
6, the cinematic letterbox hidden, 0x66F8F25 posted: the panel comes at once, as on any later visit of the menu (no
replay of the 10 s intro). Log "back in the galaxy menu with its intro never ended", `coop.status` "menu intro ended at
the return N". Off (`coop.fix world.menu_intro_return off`): as 0.7.15. adventure_tutorial no longer calls
`coop.menuintro` (89f9e9f8; the command stays for diagnosis).

## Verification
menu_panel (coop_menu_panel.py): FAIL with SPORE_FIX_OFF=world.menu_intro_return: lab 20261118-135136 (the host's intro
state stayed 5, "Galactic Adventures" not shown in 25 s; key frame 01_menu_host.jpg: only the galaxy, the Spore button
and Register). PASS: lab 20261118-135316 and 20261118-135431 (the host's state 5 -> 6 within a second of the menu,
the panel at once; key frames show Play / Create / Share / Galactic Adventures / Co-op on both games).
Regression, lab 20261118-135431 (lanes 7 and 9, all OK): adventure_tutorial (without `coop.menuintro`: "both games show
the main menu panel", the fix acted on the host; key frames in the adventure with both captains), adventure_duo,
menu_splash, tribe_intro_camera and creature_intro_camera (BUG-0626 / BUG-0607), menu_panel. In these runs only the
host's quick load hit the case; the guest (joining later) had state 6 already.

## History
- 2026-11-18: opened (fix/w53-advorn, found while testing BUG-1254; the test works round it with `coop.menuintro`). Not fixed here: idea: remember a refused GGE_intro (BUG-0626) and, when the galaxy menu comes back, send the video's end there (or skip it as Escape does).
- 2026-11-18 (fix/w53-menuintro): root cause (the refused intro leaves intro state 5, the panel needs 6); fixed with world.menu_intro_return (the intro's skip in the menu); test menu_panel; adventure_tutorial without coop.menuintro.
- 2026-11-18: released in 0.7.15.2.
