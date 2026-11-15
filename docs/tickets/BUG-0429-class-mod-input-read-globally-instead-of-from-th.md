---
id: BUG-0429
title: class: mod input read globally instead of from the game window (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content
commits: 3ff7a2da
tests: window_input, window_input_lowfps
signature:
docs: docs/08-ui.md#ui-window-input
related: BUG-0713, BUG-0090, BUG-0145, BUG-0149, BUG-0482
---

## Report
Fixed per case: BUG-0090, 0145, 0149. Not covered: F9 toggles the co-op window from any program (coop_ui.cpp:962 GetAsyncKeyState, no focus check) (obs); Sporepedia search, editor naming, save-name dialogs.
General fix: all mod input from the game window's messages while focused. Outcome check: key/click in another window changes nothing.
Source: docs/generalisation-plan.md G13.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11): F9 (GetAsyncKeyState, no focus check), the social digits (GetAsyncKeyState) and the Civ / Space RTS marker (GetCursorPos) still read the whole desktop. The Sporepedia search, editor naming and save-name dialogs are the game's own fields; the mod's own text and clicks already came from the window's messages (BUG-0482, BUG-0145).

## Fix
Shared method: mouse.h is the one way the mod reads input (the window's own WM_KEYDOWN / WM_LBUTTONDOWN / WM_CHAR / WM_MOUSEMOVE through the thread message hook). Child BUG-0713 fixed (input.window).

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
- 2026-11-08: released in 0.7.11.
