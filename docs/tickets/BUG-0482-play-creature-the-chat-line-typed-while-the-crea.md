---
id: BUG-0482
title: play_creature: the chat line typed while the creature lives loses a line now and then (1 of N), with and without the effect relay
status: fixed
kind: test
stage: Creature
reported: 2026-11-03
source: nightly detector run 2026-11-03
role: both
versions: 
fixed_in: 0.7.4 (caption), unreleased (modal boxes)
branch: fix/w40-play, fix/w41-creature-left
commits: fbe3916a, 1c880451
tests: chat_caption, chat_modal, play_creature
signature:
docs: docs/re/creature.md#creature-chat-under-caption, docs/re/creature.md#creature-chat-under-modal
related: BUG-0273, BUG-0500
---

## Report
play_creature failed with `the chat line takes the typing while the creature lives (1 lines lost; while dying: 0)` in out/lab/20261103-012624 (2 of 3 runs, main 0.7.3.6) and in out/lab/20261102-232023 (rel/0.7.3.6b, no relay): the typing of one chat line does not reach the chat while the bot plays. Input focus / the chat box opening race (G13 input), or the bot types before the box is open.
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (logs of out/lab/20261103-012624 play_creature#1/#2 and 20261103-124913 #2): every lost line was
typed while the player's game showed a caption: `busy=cinematic,input_lock` on the trace lines around it
(the brain upgrade "Brain power!" caption, cinematic F6A4B1E1, after 100 DNA; the bots did not answer it,
BUG-0500), hp full, not dying. Under the game's input lock (cGameInputManager +0x110 > 0) the game drops every
click on the UI (docs/re/creature.md#creature-input-lock-clicks); core.ui_locked_clicks (BUG-0273) presses
buttons for it, but a click on the chat line only gives it the focus, and that click was dropped too: the
line never took the focus and the typing went nowhere. A real player who opens the co-op window during a
caption loses the line the same way: a game bug, not the bot typing too early.
Reproduced (observed): coop_chat_caption.py, CRG_ClaimNest on one game (input_lock=1), F9, one click on the
chat line, `text`: the line stays empty on both the host and the guest (out/lab/20261103-151853).

## Fix
coop_ui.cpp `locked_edit_tick` / `locked_edit_apply`, mouse.cpp `take_chars`: a click under the input lock on
a visible text line of the co-op window makes the mod take the typing: the game window's WM_CHAR messages
(seen by the WH_GETMESSAGE hook, real keys and the tests' input alike) go into that line (Backspace erases,
Enter sends the chat line), outside the frame. It stops at the next click, when the window closes, or as soon
as the game changes the line's text itself (never typed twice). Switch `coop.fix core.ui_locked_edit off`.
Follow-up (fix/w41-creature-left): a modal box of the game (the window manager's modal window +0x82C, the
"game saved" box) counts as locked too, for the buttons (BUG-0273's path) and the text lines (coop_ui.cpp
`clicks_locked`). Switch `coop.fix core.ui_modal_edit off`.

## Verification
chat_caption (tools/tests/coop_chat_caption.py), both players, during CRG_ClaimNest and without a caption:
- FAIL with SPORE_FIX_OFF=core.ui_locked_edit: out/lab/20261103-151853 (`the chat line takes the typing ('')`
  for the guest and the host during the caption; without a caption OK);
- PASS with the fix: out/lab/20261103-152037 (the line in both chats, typed once, `the mod takes the typing`
  in both logs).
chat_modal (tools/tests/coop_chat_modal.py), the host under its "game saved" box (coop.save hold) and without it:
- FAIL with SPORE_FIX_OFF=core.ui_modal_edit: out/lab/20261103-235548 (the chat line stays empty under the box);
- PASS: out/lab/20261103-235444.

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-play (the click on the chat line under a caption's input lock; the mod types).
- 2026-11-03: seen again on rel/0.7.4 (out/lab/20261103-224540 play_creature): one line lost while the host's "game saved"
  box was up (the host saved the world for a guest coming back from the main menu); the same class as the caption
  (a modal box of the game takes the click), not covered by core.ui_locked_edit. Next: 0.7.5.
- 2026-11-03: released in 0.7.4.
- 2026-11-03: the modal-box half fixed on fix/w41-creature-left: the "game saved" box is the window manager's modal window (+0x82C, no input lock); core.ui_modal_edit takes it like the input lock (buttons and text lines).
