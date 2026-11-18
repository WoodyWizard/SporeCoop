---
id: BUG-9205
title: Creature: the return from the evolve editor is a long black screen with no loading screen
status: fixed
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: a8a3ceca
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14)
signature:
docs:
related: BUG-9202 BUG-9181
---

## Report
Owner play test 2026-11-14: when a guest finished evolving it was stuck on a black screen for a while. The owner asked whether it is loading and whether it can get the loading screen.

## Root cause
Observed (guest log, the QA copy capped at 10 fps): `stage: Editor -> Creature` at 18:54:35.9, then `busy=cinematic,input_lock,paused`, `cinematic,input_lock` until control came back at 18:54:57.6, with 200-530 ms stalls while the new body builds. It is not a world load: it is the game's own fade from black and the egg-hatch scene after the editor, stretched by the 10 fps cap and the stalls. The BUG-9202 cover starts only on an arriving world (reload) or the menu with the host playing (join), so it never starts here. Fix idea: start the cover on the editor-to-stage step while the frame is dark, end it on the brightness readback alone (keep the hatch scene visible, BUG-9181).

## Fix
coop.cpp on_overlay: the BUG-9202 loading screen starts on the step from the Creature editor back to the stage ("Evolving", "Building your new body...") and ends 0.5 s after the frame is no longer dark (the brightness readback), so the egg scene shows. Found in the test: the black timer of the previous cover was not reset, so the first build ended this cover at once (1.1 s, brightness 4); every cover now starts its own. Switch: `coop.fix ui.editor_cover off`.

## Verification
no-fail-run: manual on TITAN. FAIL (19:40, the build before): the guest's screenshots after the editor showed 2 frames of plain black, then the egg. PASS (20:00, a8a3ceca): screenshots every 0.8 s: the ring with "Evolving" over the black for 2.7 s, then the egg scene; log `the editor return loading screen ended (playing) after 2.7 s ... brightness 50`.

## History
- 2026-11-14: opened.
- 2026-11-14 (night): fixed (decision 3V A), live on TITAN.
