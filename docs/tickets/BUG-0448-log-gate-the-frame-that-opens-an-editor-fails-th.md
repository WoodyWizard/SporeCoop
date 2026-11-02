---
id: BUG-0448
title: log gate: the frame that opens an editor fails the hitch gate (logged just before its stage line)
status: fixed
kind: tooling
stage: all
reported: 2026-11-01
source: lab 20261101-223011 vote_net_4p
role: host
versions: main f0cefbaa (0.7.3.4 rc)
fixed_in: 0.7.3.4
branch: fix/w37-vote-leaver
commits: 324752dc
tests: gates_selftest (make test), vote_net_4p
signature:
docs:
related: BUG-0447
---

## Report
vote_net_4p in lab 20261101-223011 (lane 3, fix/w37-vote-leaver at main f0cefbaa) failed the log gate
`hitch: 1 on host: Creature: hitch 2522 ms` besides its own check (BUG-0447).

## Root cause
Observed (the host's log): `vote: end: everybody is ready` (the evolution vote) at s 66.559, `perf: hitch
2522 ms (Creature)` at s 69.080, `stage: Creature 68AC222A -> Editor` at s 69.109 (29 ms later). The frame
that opens the editor is logged as a hitch of the stage it leaves, before the stage line, so the gate's
settle window (`hitch_settle_s`, from the stage line on) had not started and the activity had no busy word.
Inferred: the editor's load is longer under the lab's load (three 4-player tests at once that evening);
the run before (20261101-210646) opened the same editor without a 2 s frame.

## Fix
tools/loggates.py: a hitch logged up to `hitch_switch_s` (0.5 s) before a `coop: stage:` line is that
switch's own frame and is left out (gates= per test can set it to 0 for the old rule). A hitch 3 s before a
stage line still fails. Judge only, no game code.

## Verification
gates_selftest (make test): "a hitch 29 ms before a stage line is the switch" FAILS with the loggates.py of
main f0cefbaa and PASSES with the change; "a hitch 3 s before a stage line still fails" passes on both.
Lab: 20261101-223011 vote_net_4p failed the hitch gate (FAIL); `loggates.py scan` of its host log finds no
hitch with the change; vote_net_4p 20261101-223718 #1, 20261101-224307 #1/#2 pass the gates (PASS).

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w37-vote-leaver (test / judge only).
- 2026-11-02: released in 0.7.3.4.
