---
id: BUG-1341
title: Creature, own DNA: a party gain reaches a guest's own DNA one total late, its cause on the total that did not move it (guest log cause=unknown)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-20
source: lab 20261120-041304 dna_notice
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w60-dna
commits: 1165c4fd
tests: dna_notice (twin dna_notice_cause_off)
signature: guest log "dna: own N -> N, bar x -> party y cause=<cause>" followed by "dna: own N -> M ... cause=unknown"
docs:
related: BUG-1291, BUG-0389, BUG-1340
switch: creature.own_dna_cause
---

## Report
rel/0.7.16 worktree out/lab/20261120-041304, dna_notice.guest.game.log: every party gain reached the guest in two totals:
  09:56:04.964 dna: own 0 -> 0, bar 0.0 -> party 3.5 cause=hunt:1+4
  09:56:04.964 dna: own 0 -> 4, bar 3.5 -> party 3.5 (kept) cause=unknown
the same in dna_away.guest.game.log 10:09:37.127 / .142 ("cause=host:0+4", then "cause=unknown"). The notices on the
screen were right (they come from the first total's causes); the guest's DNA moved one total later and its log named
no cause for the DNA it got.

## Root cause
Observed (coop.cpp on_frame order, dna.cpp): frame_send_totals (the host's MSG_DNA, with g_dna_out's causes) runs
before frame_stage_ticks, whose own_dna_tick credits the host's bar step to every own wallet. A gain of frame N was
sent in frame N+1 with its causes and the wallets not yet credited; the credit came in the same frame's stage phase,
the next total carried the wallet's step and no causes (g_dna_out was cleared). Host log dna_away.host.game.log:
37.102 "dna: 0 -> 4 cause=host:0+4", 37.106 "party +3.5 -> every own DNA".

## Fix
`creature.own_dna_cause` (dna.cpp own_dna_credit_now, called first in frame_send_totals): the totals phase runs the same
income observer before the totals go out (a second call in a frame sees no step), only while the host plays its creature
in this frame and the last and the store is the one the observer read (a load or an editor in between: the stage phase
starts the new baseline as before). The guest then gets "own 0 -> 4, bar 0.0 -> party 3.5 cause=hunt:1+4" in one total.
Off: the credit waits for the stage phase (0.7.16 before). `coop.dna own` prints cause=<switch> early=<credits>.
Test: coop_dna_notice.py asks for both causes on guest lines that MOVED its DNA.

## Verification
Was waiting for the lab run (no lab today). PASS expected: dna_notice, dna_notice_shared (and the DNA set: dna_own,
dna_own_host_editor, evo_bar, dna_pace, dna_away). FAIL expected: dna_notice_cause_off ("the guest's log names both
causes on the totals that moved its DNA").
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_notice dna_notice_shared dna_own dna_own_host_editor dna_own_reload evo_bar dna_pace --lane 2 --lane 3 --retry 0 --no-build
  SPORE_DETECTOR=20 python3 tools/lab.py run dna_notice_cause_off --lane 2 --retry 0 --no-build
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): dna_notice, dna_notice_shared, dna_own, dna_own_host_editor, dna_own_reload, evo_bar and dna_pace PASS in 20261120-133703, 20261121-003202, 20261122-194210, 20261123-023643; the twin dna_notice_cause_off FAIL in 20261120-130608 ("the guest's log names both causes on the totals that moved its DNA"). Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
