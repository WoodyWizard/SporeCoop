---
id: BUG-0984
title: tests: key frames shot right after a pause menu closed show the half-transparent menu fading out (keyframe_settle)
status: fixed
kind: test
stage: Creature
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 (key-frame review)
role: 
versions: 0.7.13-rc
fixed_in: 0.7.13
branch: fix/w48-fui
commits: f4fa2eb9
tests: keyframe_settle
signature:
docs:
related: BUG-0924
---

## Report
Key-frame review of the rel/0.7.13 final check (lab out/lab/20261112-175447, sheet
033_keyframe_settle_01_settle.jpg): the guest's frame shows the game's pause menu half-transparent over the game
(Spore Guide, Spy Glass, Back, Settings, Save, Exit, Quit): what that frame must not show.

## Root cause
Observed: keyframe_settle opens the guest's pause menu, asks for a key frame and closes the menu 6 s later; the
frame waited until no game was busy (BUG-0924) and was shot 0.4 s after the close (log: closed at +6.2 s, entry
at +6.6 s). The game says it plays again (`busy` empty, time running) at once, while its menu still fades out
for a moment: the shot caught the fade. Not a player-visible bug (a player sees the same fade).

## Fix
tools/tests/twoinst.py keyframe_settle: after a wait for a busy game the frame waits SPORE_KEYFRAME_FADE s more
(default 1.5; 0 = the old behaviour) and checks again; the manifest entry gets `shot_t` (when the shots were
asked for). keyframe_settle checks the guest was shot at least 1.2 s after the close.

## Verification
FAIL (SPORE_KEYFRAME_FADE=0): out/lab/20261112-212320-2 (shot 0.4 s after the close). PASS:
out/lab/20261112-212439 (1.86 s after the close).

## History
- 2026-11-12: opened.
- 2026-11-12: fixed in the test helper; fix/w48-fui.
- 2026-11-12: released in 0.7.13.
