---
id: BUG-0516
title: the evolution vote: the host's editor could open at once now that no reload follows it (relax the guest's ready, design)
status: open
kind: feature
stage: Creature
reported: 2026-11-03
source: fix/w40-no-reload (BUG-0508 question)
role: all
versions: 
fixed_in:
branch: fix/w40-no-reload
commits:
tests:
signature:
docs: docs/re/creations.md#creations-evolve-in-place
related: BUG-0508, BUG-0205, BUG-0318
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
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

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
