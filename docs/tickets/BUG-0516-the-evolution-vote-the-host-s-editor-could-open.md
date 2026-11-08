---
id: BUG-0516
title: the evolution vote: the host's editor could open at once now that no reload follows it (relax the guest's ready, design)
status: fixed
kind: feature
stage: Creature
reported: 2026-11-03
source: fix/w40-no-reload (BUG-0508 question)
role: all
versions: 
fixed_in: 0.7.11
branch: fix/w46-e1-features
commits: 3365fc83 ec5aa499
tests: editor_now (coop_editor_now.py)
signature:
docs: docs/re/creations.md#creations-evolve-in-place, docs/09-coop-design.md, docs/06-testing.md#testing-switch-vote-editor-now
related: BUG-0508, BUG-0205, BUG-0318, BUG-0315
switch: core.editor_now
---

## Report
The player of BUG-0508 asks why the guest must be ready (the vote) for the host's editor. Answer in
docs/re/creations.md#creations-evolve-in-place: the vote opens every guest's own editor together with the host's
(their editor turn in the stage), and the host's game (the NPC simulator) stands while it edits; the third reason,
the reload that followed the editor, is gone with BUG-0508. "Start now" and the 60 s limit already let the host go.

## Root cause
Design question, not a bug. Relaxing it (the host's editor at once, a guest's own editor offered as a button
while the host is away) touches BUG-0205 (host away: the guest's own NPC simulation), BUG-0318 (editor after a body
swap), the own editor scheduling (own_editor_tick) and the DNA of the guest's editor (BUG-0466). Not changed: not
clearly safe without its own tests.

## Fix
`core.editor_now` (coop.cpp h_Submit, MSG_VOTE flags): the host's request in the Creature stage with the evolution tag
(+0x90 0x64AA168, the mating call's request) goes to the game at once (only the existing save / new-body holds), no
vote. Every guest gets an offer (MSG_VOTE with active 0 and flags 1: older guests ignore it): a notice and the co-op
window, whose ready button reads "Evolve now" for 90 s; the click sets the guest's own editor due, and
own_editor_tick opens it as after a vote (settled stage, busy gate, BUG-0318 new body, BUG-0466 DNA charge; all are
checked when it opens, not at the vote's moment). A guest that does not take it plays on: the host in its editor
counts as away (BUG-0205, its NPCs simulated by the guest's game), and the guest can evolve at its nest later. The
first Creature editor, Creature -> Tribe, the Cell stage and the design stages keep the vote. Off: the vote as in
0.7.10. Kept behind the switch so it can become a session setting (docs/plan-0.7.12.md F3).

## Verification
`editor_now` (coop_editor_now.py, lane 5): the host's editor 1 s after `coop.vote evolve`, the guest's window shows
Evolve now while its game stays in Creature, the host is done first, the guest's click opens its own editor while the
host plays, the guest leaves it.
* PASS: out/lab/20261108-035852.
* After the K2b merge (rel/0.7.11 fa4f5df3) the test failed at its last step: the own editor has no red cross since
  creations.editor_request (docs/06-testing.md#testing-own-editor-no-cross); the guest now leaves with done
  (fix/w46-e1-merge): PASS 3/3 out/lab/20261108-081737 (lane 4), FAIL with `SPORE_FIX_OFF=core.editor_now`
  out/lab/20261108-082329.
* FAIL with `SPORE_FIX_OFF=core.editor_now`: out/lab/20261108-040059 (the host still in Creature 16 s after the
  request, the guest's window showed I'm ready).

## Decision (2026-11-05, fix/w44-coop-window, U1 of 0.7.8)
Read again (coop.cpp start_vote / finish_vote / submit_tick / own_editor_tick, docs/re/creations.md#creations-evolve-in-place):
not implemented in 0.7.8, it is not small. What it would take:
1. A switch (`core.editor_now`) in start_vote: with it the host's editor request is not held (`hold_submit` at once,
   no Vote), and each guest gets a notice plus a lobby button "Evolve now" instead of the ready vote.
2. The guest's own editor offered while the host is away in its editor: own_editor_tick today opens it only from
   the vote's end (`editor_pending`); it would need its own trigger and its own busy gate (BUG-0404), and the
   guest's NPC simulation while the host stands in the editor (BUG-0205: the host's simulator is the world's; a guest
   in its own editor at another time sees a world that moves on without it, then the host's share after the editor).
3. The guest's editor after a body swap (BUG-0318, `avatar_swap_settling`) and the guest editor's DNA (BUG-0466)
   are tied to the vote's moment today; both need a check that they hold at any time.
4. Tests: a two-instance test of "host evolves alone, the guest evolves later" (both bodies on both games, no
   reload, the DNA of each), plus the existing vote tests (vote, vote_caption, vote_during_cinematic,
   vote_end_editor, vote_drop_editor) with the switch on and off.
Until then "Start now" (the host's button during a vote; BUG-0597 keeps it enabled) and the 60 s limit let the host go.

## History
- 2026-11-03: opened.
- 2026-11-05 (fix/w44-coop-window): read again; not small and safe: what it takes written under Decision.
- 2026-11-08 (fix/w46-e1-features, E1 of 0.7.11): implemented as decided (the offer through the ready button), core.editor_now.
- 2026-11-08: released in 0.7.11.
