---
id: BUG-0551
title: Creature: a chat line typed while the host is paused saving the world for a returning guest is lost (the mod takes the typing, the line stays empty)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: final check out/lab/20261104-135838 play_creature
role: host
versions: 
fixed_in: 0.7.8
branch: fix/w44-coop-window
commits: 9cdebdf0
tests: chat_pause (coop_chat_pause.py)
signature:
docs: docs/08-ui.md
related: BUG-0482, BUG-0599
---

## Report
play_creature in the 0.7.6 final check (out/lab/20261104-135838): `the chat line takes the typing while the creature lives (1 lines lost; while dying: 0)`. The host's line at 14:21:33 was lost while the host was paused (`busy=paused`) saving and sharing the world for the guest that came back from the main menu (14:21:26 to 14:21:30); the host log has `a click on a co-op text line while the game's input is locked (a caption): the mod takes the typing` twice, the line stayed empty. Reproduce: play_creature, a chat line typed by a bot during the share save after the guest's exit_to_menu.

## Root cause
The lab run of the report is gone (out/lab/20261104-135838 not kept); found in the code and reproduced in
coop_chat_pause.py (2026-11-05). Observed, two ways the mod's typing of the line (core.ui_locked_edit /
core.ui_modal_edit, BUG-0482) lost what was typed under the "game saved" box:
1. locked_edit_tick ends the mod's typing at any click that is not on a text line; world.cpp confirms the box with
   its own `click` on the box's OK, so the player's typing after the confirm went nowhere (the line never had the
   game's focus): FAIL run line 'host pause 501' without ' and after'.
2. on the frame of a click the keys were dropped ("went to whatever had the focus before"); a frame held by the
   save (perf hitch ~600 ms, and the box can hold the frames) takes the click and the keys after it at once: the
   whole line was lost (FAIL run: the line holds '').
Inferred: play_creature's bot (click, 0.6 s, type, check 0.3 s later, twice) hit 1 or 2; both clicks were taken by
the mod ("the mod takes the typing" twice in the host log). Also found: the mod could not confirm the box at all
while the co-op window was open (BUG-0599), which keeps the host paused while the window is up.

## Fix
`core.ui_locked_edit_keep`: world.cpp announces its click (`mouse::own_click(x, y)`, the hook marks the next click
there within 5 s as the mod's); such a click does not end the typing. `mouse::take_chars(&after)` also returns the
keys that came after the last click of the frame; when that click starts the mod's typing they go into the line.
Enter in the line sends it as before (also after the pause). Off: both lost (0.7.7).

## Verification
Lab: chat_pause FAIL with `SPORE_FIX_OFF=core.ui_locked_edit_keep` out/lab/20261105-100106 (part 1: 'host pause 501'
without the second half, not in the chat; part 2: the line holds ''), PASS out/lab/20261105-100001 (both lines whole
in both players' chat).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-tests): G1: read coop_ui.cpp's locked-edit path (locked_edit_tick / locked_edit_apply); not reproduced, not changed.
- 2026-11-05: fixed on fix/w44-coop-window (core.ui_locked_edit_keep), test coop_chat_pause.py.
- 2026-11-05: released in 0.7.8.
