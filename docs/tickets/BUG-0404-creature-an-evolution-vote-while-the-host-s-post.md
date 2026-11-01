---
id: BUG-0404
title: Creature: an evolution vote while the host's post-editor cinematic plays opens an editor without its buttons; the host is stuck
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: coop_editor_soak.py (w33)
role: host
versions: 0.7.3.3 (main a05d1f6)
fixed_in:
branch: feat/w33-tests-creature
commits:
tests: tools/tests/coop_editor_soak.py (seen in its first run before it waited for the cinematic)
signature:
docs:
related: BUG-0371, BUG-0273, BUG-0019
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
Observed: the cinematic of the Creature stage does not end while the game is in the editor mode, and its input
lock keeps the editor's UI from being built or used. Inferred: the vote (or the editor request) does not wait
for a cinematic that holds the input. Not checked: whether a player can start an evolution vote during this
cinematic (the test started it with `coop.vote editor`; a guest's vote or the host's mating call during the
cinematic would do the same), and whether the cinematic ends after a long wait.

## Fix
Not fixed (outside this test branch). Candidate: a vote (or its editor request) waits while a cinematic holds
the input lock (as the world share already does, BUG-0019); or the cinematic is ended before an editor opens.
The soak test now waits for the cinematic like a player (Enter) between rounds.

## Verification
Repro: two games on the creature fixture; `coop.vote editor` on the host, the guest's "I'm ready", "done" on both
(1416 1062); as soon as both are back in the stage, `coop.vote editor` again and ready: the host's editor has no
buttons. Not in a suite test yet (the soak test avoids it).

## History
- 2026-11-01: opened.
