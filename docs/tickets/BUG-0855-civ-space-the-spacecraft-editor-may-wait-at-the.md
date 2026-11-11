---
id: BUG-0855
title: Civ -> Space: the spacecraft editor may wait at the busy gate under the game's own launch pause (suspected)
status: open
kind: freeze
stage: Civ
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9070
role: host
versions: 0.7.7 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: the editor waits: the game is busy (paused), editor 0x96B24187
docs: docs/re/civ.md#civ-shared-message-box
related: BUG-0576, BUG-0404, BUG-0311, BUG-0578
---

## Report
After the Civ -> Space vote the host may stay in Civ on a paused, input-locked screen while the spacecraft editor
request waits, until the busy gate's limit runs out. The fork saw it (editor 0x96B24187, input_lock + paused, no
cinematic) after the vote closed.

## Root cause
* Observed (code): BUG-0576 core.editor_own_pause (coop.cpp:1771) masks the input lock the game had at Submit, but
  the pause only while GetPauseCount(0x4BF38A6) > 0 (the Tribe editor pause reason).
* Unknown: which pause reason the Civ launch uses. If it is another one, the pause still holds the spacecraft editor.
  Our campaign_space test passes through this editor but, as far as known, does not measure the delay.
* Suspected, confidence low-medium.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9070, switch civ.launch_editor); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

The fork identifies the native request exactly: editor +0xC = 0x96B24187, model +0x10 = 0, return mode +0x1C = kCiv,
context +0x90 = 0x0116D51D, in Civ, plus its own "space ready" check, and for that request masks kPaused and
kInputLock at the gate (`native_editor_owns_pause`). Real pause-menu / dialog UI is checked again outside the frame
before Submit (pause_ui_up: then the request is held again), and cinematics still wait. For us: first measure; if
the pause reason differs, prefer adding the Civ launch pause reason to our generic BUG-0576 rule over a request
whitelist.

## How to test
campaign_space (or a shorter Civ fixture with all cities captured): measure the time from the end of the Civ ->
Space vote to the host's editor mode; assert < 15 s like tribe_civ. If it FAILs on main, the fix must make it PASS
(switch off = FAIL). Log GetPauseCount for the reasons active at Submit to name the Civ pause reason in
docs/re/civ.md.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0578 (a crash in Model::Release at game exit with a held editor request, 0.7.3.6) is what a player may hit when this hold makes them quit; the exit test exit_editor_held should be run with this editor once the hold is reproduced.
