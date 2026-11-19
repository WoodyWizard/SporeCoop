---
id: BUG-1318
title: dna_own tests confirmed the brain-up caption for 12 s only: its Enter is taken about 16 s after the gain, the guest stayed in the cinematic and its own editor never opened (dna_own_editors)
status: open
kind: test
stage: Creature
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: dna_own_editors, dna_own_editors_off
signature:
docs:
related: BUG-1291
---

## Report
dna_own_editors failed "the guest's own editor opened" in 20261119-104136 and again in 20261119-111652: the guest took
the host's Evolve now offer ("your editor opens in a moment", editor_pending=465C50BA) but its game stayed
`busy=cinematic,input_lock,paused` for the rest of the test. The failure screenshot shows the guest's brain-up caption
("Brain power! ...") with its green check still up. Its twin failed the same way.

## Root cause
✔ (runtime probe on lane 2, 2026-11-19) `prepare(need=110)` adds DNA past the first brain level: both games play the
brain-up cinematic. Its caption takes Enter only once it shows, about 16 s after the gain (the game pauses then:
`busy` gains `paused`); Enter on the guest before that does nothing, Enter after it closes the caption at once (the
probe: host closed at 19 s, guest at 30 s). The test pressed Enter for 12 s (from 3 s after the gain): the host's
caption was closed by later steps, the guest's never; the own editor (BUG-0516's offer) waits for the guest's stage
to settle, so it never opened. A bug of the test's set-up, not of the mod.

## Fix
coop_dna_own.py prepare: Enter on every game whose cinematic still plays, every 2 s, until neither game has one (at
most 60 s), and the log says when they closed. The checks are unchanged.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
