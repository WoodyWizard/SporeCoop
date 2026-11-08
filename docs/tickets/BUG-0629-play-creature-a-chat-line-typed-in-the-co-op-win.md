---
id: BUG-0629
title: play_creature: a chat line typed in the co-op window and sent with the Send button reaches neither chat (1 of 3)
status: fixed
kind: test
stage: Creature
reported: 2026-11-06
source: fix/w45-last play_creature out/lab/20261106-130130 #3
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-p4-creature
commits:
tests: chat_held_send, chat_held_send_oldcheck (tools/tests/coop_chat_held_send.py), play_creature (play_creature.chat_reached)
signature:
docs: docs/06-testing.md#testing-chat-held-send
related:
---

## Report
play_creature (fix/w45-last, out/lab/20261106-130130, play_creature#3, lane 4) FAIL "chat lines typed in the co-op window
reach both players (2/3)": the host's bot typed "host hunting 448" (coop.uistate showed it in the chat edit), clicked
Send; 1.5 s later neither game's chat (coop.status `chat:` lines) had it (mine ['* Bob joined', 'Bob: guest hunting
514'], theirs the same). The run's other checks passed. Not the BUG-0482 symptom (that one lost the typing). Not
analysed: whether the Send click missed (the window moved / closed by a dialog), or the line was sent and lost.
Reproduce: `tools/lab.py run play_creature --repeat 4` (1 of 4 runs here).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not a mod bug: the line was sent and reached both games; the bot looked too early. Observed in the run's game logs
(out/lab/20261106-130130 play_creature#3, fix/w45-last): the host's game held its frames in a row of hitches
(13:31:57.6 423 ms, 58.25 564 ms, 59.38 1115 ms), the bot's Send click waited in the window's queue and the game took
it at 13:31:59.452 ("ui: click send", "<Alice> host hunting 448"); the guest showed the line at 13:31:59.475. The bot
looked once, 1.5 s after its click (play_creature.py), and called the line lost. The mod writes the line at once
when the click is taken (coop_ui.cpp do_send_chat); a player sees it after the held frames, as the game itself.

## Fix
The bots' check (play_creature.chat_reached): a line not there 1.5 s after the click is looked for again until both
games have run 5 frames since that look (the console's `frame` counter, answered off the main thread) or 8 s; only
then it is lost. A late line is counted (`chat_late`) with a note. `SPORE_CHAT_WAIT=0`: the old single look.
Reproducer `coop_chat_held_send.py`: the host's main thread held 2.5 s (`kit.test.hang`), the Send click during the
hold, the line must be on both games (and in the guest's game as the host's line).

## Verification
FAIL (the old check, SPORE_CHAT_WAIT=0): out/lab/20261107-210110 chat_held_send_oldcheck#1, #2 (both lines "on both
False" at 1.5 s). PASS: out/lab/20261107-210110 chat_held_send#1, #2 (both lines on both games 2.8 s after the click).

## History
- 2026-11-06: opened.
- 2026-11-07: fix/w46-p4-creature: explained (the click taken after held frames, the line on both games 0.4 s after the bot's single look); the bots' check waits for the frames; chat_held_send FAIL/PASS.
