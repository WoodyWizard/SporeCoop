---
id: BUG-0855
title: Civ -> Space: the spacecraft editor may wait at the busy gate under the game's own launch pause (suspected)
status: fixed
kind: freeze
stage: Civ
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9070
role: host
versions: 0.7.7 .. 0.7.11.1 (code read)
fixed_in: 0.7.7
branch: fix/w48-tribe-civ
commits: dc4084f5
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
Reproduced with BUG-0576's switch off; fixed since 0.7.7 by BUG-0576's rule.
* Observed (runtime, campaign_space, the Civ -> Space change with four players): the spacecraft editor request
  96B24187 comes from the Civ -> Space vote ("Alice wants to edit a design (Civilization)"); the game submits it under
  its own pause and input lock, and the pause reason is **0x4BF38A6** (GetPauseCount 1 at the gate), the same reason
  as the end of Tribe. With `core.editor_own_pause` on the host logs "the editor does not wait for the game's own
  input_lock,paused (... editor pause count 1)" and is in the editor 1.3-1.4 s after the vote (lab 20261112-102046-2;
  rel/0.7.13 20261112-050529 and five more runs on other branches the same).
* With `core.editor_own_pause` off (the code before 0.7.7): "the editor waits: the game is busy (input_lock,paused)"
  and the host's editor came up 61 s after the vote (lab 20261112-100520, 20261112-102046): the fork's hold.
* The fork (PR #196 BUG-9070) is based on our 0.7.3.6, before BUG-0576 existed.

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
Already in 0.7.7: BUG-0576's `core.editor_own_pause` lets a vote's editor through the pause and input lock the game had when it submitted the request while its editor pause reason 0x4BF38A6 is counted; the Civ launch uses that reason. This release adds the timing check to the campaign (BUG-0855) so the test fails without it. Not run: exit_editor_held with the spacecraft editor (BUG-0578): it needs the hold, which exists only with the switch off.

## Verification
`coop_campaign.py` (suite `campaign_space`, four players, Civ -> Space, ~14 min) now checks in every stage change
that the spacecraft (96B24187) and City Hall (99E92F05) editors come up within 15 s of their vote (the host's
stage change to the editor mode); a later one is an `editor` finding (FAIL).
* FAIL with `SPORE_FIX_OFF=core.editor_own_pause`: lab 20261112-102046 (61 s; also 20261112-100520 before the check: 61 s).
* PASS: lab 20261112-102046-2 (1.4 s), 20261112-103545 (1.0 s).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10 (plan/w47-closed-open, closed-ticket review 2026-11-10): closed-ticket review: BUG-0578 (a crash in Model::Release at game exit with a held editor request, 0.7.3.6) is what a player may hit when this hold makes them quit; the exit test exit_editor_held should be run with this editor once the hold is reproduced.
- 2026-11-12 (fix/w48-tribe-civ): not reproduced: the Civ launch pauses with the editor reason 0x4BF38A6, which BUG-0576 masks (campaign_space host logs: the editor 1.3 s after the vote).
- 2026-11-12 (fix/w48-tribe-civ): reproduced with core.editor_own_pause off (61 s); fixed since 0.7.7 by BUG-0576 (the launch pause is reason 0x4BF38A6); campaign check added.
