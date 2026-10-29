---
id: BUG-0273
title: vote: the I'm ready clicks of all three guests were not counted, the Creature -> Tribe vote ended by its timer
status: fixed
kind: bug
stage: creature
reported: 2026-10-29
source: lab 20261029-122937 campaign
role: 
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w7-vote
commits: 2525267
tests: vote_caption
signature:
docs: docs/re/creature.md#creature-input-lock-clicks, docs/06-testing.md#testing-vote-caption
related:
---

## Report
Lab 20261029-122937 campaign#1 (lane 2, out/lane2/campaign/20261029-122947, finding 3). The host
started the Creature -> Tribe vote at 13:49:15 (game clock). Bob, Carol and Dave clicked the real
"I'm ready" widget at 3-7 s (summary 12:49:18-22, Carol and Dave twice), but the host logged nothing
until "evolution starts (time is up)" at 13:50:14; Bob's log has no line of the click either. The
first vote of the same game (Cell -> Creature, 13:39) ended "everybody is ready" in 2 s. The vote
came right after the EP shortcut and three captions per player. Not analysed (?): whether the
widget click reached the mod (button shown but the vote not yet known on the guest?).

## Root cause
Observed (runtime): every cinematic, a caption included, holds the game's input lock
(`cGameInputManager` +0x110, GOG 2024 getter 0xB3D350; incremented at the cinematic start
0xADFEC1, decremented by EndCinematic). While it is held the game drops every click on the UI,
our co-op window too. In coop_vote_caption.py a guest with CRG_ClaimNest on screen (`input_lock=1`)
clicked "I'm ready" twice, the button's handler never ran and the host's vote stayed open.
Inferred for the campaign: the Creature -> Tribe vote started at 13:49:15 while the guests still had
the brain-upgrade captions of AddEvolutionPoints up (three captions each until 12:49:11 bot time;
the bot answers a vote before captions, so the captions stayed); the first vote of the game came
after the captions. No log line shows the click because the mod never got it.

## Fix
coop_ui.cpp: while the input lock is held and the co-op window is open, a primary click from the
window's messages (mouse.h) on a visible button of the window presses that button (outside the
frame) after 0.5 s, unless the game delivered the click itself meanwhile (counted as
`locked_native`, never seen). `coop.uistate` prints `input_lock`, `locked_clicks`,
`locked_native`. Switch: `coop.fix core.ui_locked_clicks off` (the clicks are dropped again).

## Verification
- FAIL without the fix: lab 20261029-142824 (main code, lane 2), lab 20261029-143459
  (`SPORE_FIX_OFF=core.ui_locked_clicks`, lane 2): votes 1 and 2 during the caption stayed open.
- PASS with the fix: lab 20261029-143340 (lane 2), lab 20261029-143504 (lane 3).

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w7-vote (input lock drops the clicks; the mod presses the button).
