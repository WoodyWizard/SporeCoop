---
id: BUG-0404
title: Creature: an evolution vote while the host's post-editor cinematic plays opens an editor without its buttons; the host is stuck
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: coop_editor_soak.py (w33)
role: host
versions: 0.7.3.3 (main a05d1f6)
fixed_in: 0.7.3.4
branch: fix/w34-vote-cinematic
commits: ddb7f14, 3b83b29
tests: vote_during_cinematic, busy_gate (tools/tests/coop_vote_cinematic.py); npc_tests test_busy_gate
signature:
docs: docs/re/creature.md#creature-busy-states, docs/06-testing.md#testing-busy-gate, docs/known-issues.md#issue-vote-during-evolution-cinematic
related: BUG-0371, BUG-0273, BUG-0019, BUG-0331, BUG-0336, BUG-0319
switch: core.editor_cinematic, core.own_editor_busy, core.vote_action_busy
---

## Report
The first run of coop_editor_soak.py (lane 5, 2026-11-01 13:58): round 1 (`coop.vote editor`, the guest's ready,
"done" on both) came back to the stage; the host's game then played the evolution's cinematic F4F96E43
(`coop.status`: `cinematic F4F96E43 playing`, trace `input_lock=1`, `share_wait=cinematic`). The test started the
next vote 7 s later, during that cinematic: both games entered their editor, the guest left it with "done", the
host's editor was drawn without its buttons (no "done" check, no top bar; screenshot: the parts palette and the
creature only) and the host's game stayed in the editor (`game.mode` 0x00DBDBA1) with the cinematic still
"playing" and the input lock on for more than 3 minutes; Enter and clicks on "done" did nothing. A deferred
creature removal waited for the cinematic too (`destroys during cinematics: deferred=1 pending=1`).

## Root cause
Observed (lane 5, 2026-11-01, `coop_vote_cinematic.py`): after every editor the Creature stage plays
CRG_Fade_From_Black_Egg (F4F96E43) on both games, ~7 s on the host and ~10 s on the guest, with the input
lock held. A vote ends whenever the others are ready (or its 60 s are up), whatever the host's game plays;
`finish_vote` -> `hold_submit` -> `submit_tick` then submitted the host's editor request at once. An editor
opened while a cinematic plays comes up without its buttons and the cinematic never ends in the editor
mode (`cinematic F4F96E43 playing`, `input_lock=1` for minutes; Enter and "done" do nothing).
EndCinematic called then brings the buttons back.
Observed, the players' way: the guest leaves its editor, the host clicks "done", the next vote starts and the
guest presses "I'm ready" within the ~7 s of the host's fade-in: the host is stuck (case
`evolution_cinematic`, FAIL run below). The same for any cinematic or caption the host's game plays when the
vote ends (inferred for the brain-upgrade and migration captions: the same cinematic manager state).
The guest's own editor after a vote already waited for the guest's own cinematic (`own_editor_tick`,
case `guest_cinematic` passes with the switches off), but not for a pause: due while the guest's pause
menu was open it was dropped for good ("own editor not opened: the pause menu is open"); and the host's
editor opened under the Sporepedia (the game time paused) when the vote ended there (in the editor
afterwards, Esc gave a working editor: not stuck, but opened under the player's window).
Same class (ticket audit C15, busy states): "paused" was known only as the pause menu's "Resume Game"
button (BUG-0331, BUG-0336); `cGameTimeManager` +0x48 bit 0 is set by the pause menu, the Sporepedia and
My Collections alike (docs/re/creature.md#creature-busy-states).
Cell -> Creature: the same gate applies to the Cell stage's held editor (cell captions and the level-up zoom
use the same cinematic manager state, docs/re/cell.md); not reproduced with a vote in the Cell stage.

## Fix
One busy gate (mods/coop/busy.h, `game_busy()` in coop.cpp: a cinematic, the input lock, the game time
paused, an editor, a load; `busy:` in `coop.status`, `busy=` in the trace line). Each action waits for its
own mask (busy.h); a pause alone at most 300 s, a cinematic (also under a pause: a caption) 60 s:
* `core.editor_cinematic`: the host's held editor (a vote's) waits at the gate and for 3 s after an editor
  (the fade-in starts then); after the 60 s of a cinematic it is ended natively right before the editor
  opens.
* `core.own_editor_busy`: the guest's own editor after a vote waits through a pause (the 90 s limit counts
  only unpaused time) instead of being dropped.
* `core.vote_action_busy`: an action vote's result (the superweapon) waits at the gate; dropped when the
  stage was left or the session ended.
* The world share after a mode change reads the gate's cinematic mask (busy.h kShareGate: BUG-0019's wait
  and switch `npc.sharecine`, unchanged). Not a pause, on purpose: the game sets the pause flag itself at the
  Creature stage start, after an editor and under captions; a share that also waited for it (a switch
  `world.share_busy` on the way, removed) came 120 s late and guest_editor_hold failed (lab
  20261101-172428: the host's new body reached the guest after its editor had closed).
* Test tools: the campaign's sync and census checks skip a guest whose game stands by `busy:` (paused,
  editor, loading), not only by the "Resume Game" button (`OLD_PAUSE_CHECK=1`: the old check).
* Test hook: `coop.share due` (a share due as after an evolution).

## Verification
`tools/tests/coop_vote_cinematic.py`: vote_during_cinematic (host_cinematic by PlayCinematic,
evolution_cinematic the players' way, guest_cinematic, host_paused by the Sporepedia, guest_paused by the
pause menu) and busy_gate (`--part gate`: action_cinematic, action_paused, share_cinematic, share_paused);
checks the busy game's own state (its mode, cinematic, input lock in the editor, a real click on "done",
the guest's load).
* FAIL with `SPORE_FIX_OFF=core.editor_cinematic` (first version of the test, cases 1-3): lab
  20261101-161342 (host_cinematic and evolution_cinematic: the host's editor with cinematic F4F96E43 and
  input lock 1, "done" does nothing).
* FAIL with `SPORE_FIX_OFF=core.editor_cinematic,core.own_editor_busy`: lab 20261101-164848
  (host_cinematic, evolution_cinematic, host_paused, guest_paused).
* FAIL with `SPORE_FIX_OFF=core.vote_action_busy,world.share_busy` (the share's pause wait, removed since):
  lab 20261101-165714 (action_cinematic, action_paused; share_paused was the removed switch's case).
* PASS: lab 20261101-161924 (3/3, cases 1-3), 20261101-164247 (all five cases), 20261101-164000 (busy_gate),
  final code (3faabb0+): vote_during_cinematic 3/3 in lab 20261101-173721, busy_gate 3/3 in lab
  20261101-181701 (busy_gate#3 of 20261101-173721 met its checks and failed on the monitor's link rule
  over two world transfers 18 s apart: BUG-0439, the test now spaces them).
* Regression (lane 5): guest_editor_hold 20261101-173422 (it failed in 20261101-172428 with the share's pause
  wait, removed); vote, vote_caption, vote_stale, vote_end_editor, vote_drop_editor, guest_editor,
  evolve_world, civ_sw_vote, cell_editor_timed pass in 20261101-175912; editor_desync fails there at part 4,
  the known BUG-0411 (also on main).
* smoke 14/14 on lane 5: lab 20261101-182611. creature_editor_soak not run on this branch.
* `make test`: npc_tests test_busy_gate (the gate's waits and limits).

## History
- 2026-11-01: opened.
- 2026-11-01 (fix/w34-vote-cinematic): reproduced deterministically (PlayCinematic on the host while the vote
  ends) and the players' way (the next vote within the host's fade-in); fixed with one busy gate for the
  vote's editor, the guest's own editor, action votes and shares (widened on the coordinator's request after
  the ticket audit's class C15).
- 2026-11-02: released in 0.7.3.4.
