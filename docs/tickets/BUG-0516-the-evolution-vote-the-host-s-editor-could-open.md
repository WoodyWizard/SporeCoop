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

## History
- 2026-11-03: opened.
