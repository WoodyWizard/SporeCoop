---
id: BUG-0855
title: Civ -> Space: the spacecraft editor may wait at the busy gate under the game's own launch pause (suspected)
status: not-reproduced
kind: freeze
stage: Civ
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9070
role: host
versions: 0.7.7 .. 0.7.11.1 (code read)
fixed_in:
branch: fix/w48-tribe-civ
commits:
tests: campaign_space
signature: the editor waits: the game is busy (paused), editor 0x96B24187
docs: docs/re/civ.md#civ-launch-pause
related: BUG-0576, BUG-0404, BUG-0311, BUG-0578
---

## Report
After the Civ -> Space vote the host may stay in Civ on a paused, input-locked screen while the spacecraft editor
request waits, until the busy gate's limit runs out. The fork saw it (editor 0x96B24187, input_lock + paused, no
cinematic) after the vote closed.

## Root cause
Not reproduced on our code: the Civ launch's pause is the editor pause reason BUG-0576 already masks.
* Observed (runtime, the host's game logs of campaign_space, the Civ -> Space change with four players, our code
  since 0.7.7): the spacecraft editor request 96B24187 comes from the Civ -> Space vote ("Alice wants to edit a
  design (Civilization)", editor=96B24187); at the gate the host logs "the editor does not wait for the game's own
  input_lock,paused (it requested the editor from it, BUG-0576; editor pause count 1)": the game's pause count of
  reason 0x4BF38A6 is 1, the same reason as the end of Tribe. The host's game is in the editor mode 1.3 s after the
  vote ended (lab rel/0.7.13 20261112-050529: vote end 05:14:03.108, `stage: Civilization -> Editor` 05:14:04.412).
  The same in every campaign_space run looked at: fix/w48-new 20261112-074718, fix/w48-space 20261112-043001,
  fix/w48-own-world 20261112-034104, rel/0.7.12 20261111-185533 and 20261111-151535 (vote end to the gate's line in
  the same millisecond, the editor mode within 2 s).
* The fork's hold (PR #196 BUG-9070) was on its own base, our 0.7.3.6, before BUG-0576 (core.editor_own_pause,
  0.7.7) existed: there every editor request under the game's own pause waited at the busy gate.

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
None needed: BUG-0576's rule (`core.editor_own_pause`) already lets the spacecraft editor request through the game's own launch pause.

## Verification
no-fail-run: not reproducible with core.editor_own_pause on (BUG-0576 covers the launch pause: reason 0x4BF38A6,
count 1). Evidence: the host's game logs of campaign_space named in Root cause (the editor mode 1.3-2 s after the
vote). The plan's untried test idea (exit_editor_held with the spacecraft editor, BUG-0578) needs a held request,
which exists only with core.editor_own_pause off; it was not run in this release.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0578 (a crash in Model::Release at game exit with a held editor request, 0.7.3.6) is what a player may hit when this hold makes them quit; the exit test exit_editor_held should be run with this editor once the hold is reproduced.
- 2026-11-12 (fix/w48-tribe-civ): not reproduced: the Civ launch pauses with the editor reason 0x4BF38A6, which BUG-0576 masks (campaign_space host logs: the editor 1.3 s after the vote).
