---
id: BUG-0597
title: Co-op window: 'Send my world' with no players or no world loaded does nothing but says 'Sending the world to everyone'
status: fixed
kind: bug
stage: Menu
reported: 2026-11-05
source: owner's manual test 2026-11-05 (0.7.7)
role: host
versions: 0.7.7
fixed_in: 0.7.8
branch: fix/w44-coop-window
commits: afebdb63
tests: ui_explain (coop_ui_explain.py)
signature:
docs: docs/08-ui.md
related:
---

## Report
The host hosts from the main menu (Galaxy menu, 1/4 players) and clicks "Send my world": nothing happens, while
the window says "Sending the world to everyone...". There is no world loaded and nobody to send it to
(coop_ui.cpp share button calls A.share_world() unconditionally). Players read it as "solo does not start".
Expected: the button is greyed out or says why ("Load or start a game first" / "Nobody has joined yet"), and
the lobby says that a host can just start or load a game and friends can join any time.

## Root cause
Observed (code, lab 2026-11-05): the button's action called `A.share_world()` and set the status "Sending the
world to everyone..." unconditionally. `share_all()` (coop.cpp) without a loaded world (`!(g_adapter && g_local)`)
only marks the guests as waiting and posts a notice; with nobody joined it loops over no players. The lobby's world
line was empty for a host alone (`ui_world_line` speaks only with more than one player), so nothing said that a
host can start or load a game alone and friends join later.

## Fix
`Api::share_state` (0 send, 1 no world loaded, 2 nobody joined). With `core.ui_share_explain` on: the button is
greyed out (`kFlagEnabled` off) while it would send nothing; a click that still reaches it (the locked click path)
says why ("Start or load a game first: friends who join get it by themselves" / "Nobody has joined yet: friends
who join get your world by themselves"); the lobby line of a host alone in a menu reads "Start or load a game now:
friends can join any time", alone in its world "Nobody has joined yet: ...". English and Russian. Off
(`coop.fix core.ui_share_explain off`): the 0.7.7 button and status. README "How to play" says a host can play
alone at once.

## Verification
Lab: ui_explain FAIL with `SPORE_FIX_OFF=core.ui_share_explain,core.ui_creation_hint` out/lab/20261105-095220
(button enabled alone and in the menu, world line '', a click says "Sending the world to everyone..."), PASS
out/lab/20261105-095343 (share_enabled=0, the lines above, no "Sending"). Layout: out/lane6/shots/ui_explain_menu*.png,
ui_explain_alone_world.png (English and Russian fit one line).

## History
- 2026-11-05: opened.
- 2026-11-05: fixed on fix/w44-coop-window (core.ui_share_explain).
- 2026-11-05: released in 0.7.8.
