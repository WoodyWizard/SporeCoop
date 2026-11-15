---
id: BUG-1145
title: Tests: monitor 'together' fails space_allies when the guest comes back from the galaxy view (own state read up to 6 s ahead)
status: fixed
kind: test
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-tests
commits: 7c883a1d
tests: space_allies (monitor gate), gates_selftest (together_galaxy, together_space_apart, together_both_galaxy)
signature:
docs: docs/06-testing.md#testing-monitor-nowhere, docs/archive/known-issues-2026.md#issue-monitor-together-galaxy
related: BUG-1147
---

## Report
space_allies#4 of `lab.py run space_allies --repeat 12` on fix/w51-space2 (run out/lab/20261115-042849, lane 7, cold):
every check of the test passed, no fault, but the lab's gate monitor failed it: "together: guest and host play Space
80460035 by their own games, but guest sees host elsewhere (same_place=0) for 10 s from s 10". The guest's own trace
says world `-` from s 8.2 to s 20.6 (the test zooms it out to the galaxy view, `to_galaxy`, and back: 12.4 s this
time); its partner lines at s 10.0 and s 20.0 say same_place=0, and at s 20.6 (back in the system) same_place=1.
Inferred: `Together.check` takes the viewer's own state with `own_at(viewer, t, 6)` (up to 6 s off), so the partner
line at s 20.0 is paired with the own line of s 20.6 (world 80460035 again) and the one at s 10.0 with a line that
also says 80460035, which makes a 10 s "run" out of the galaxy-view stretch. Not seen before in any worktree's logs.
Seen again in out/lab/20261115-044540 (lane 6, a twin run with allies.near_rebuild off: the same text, "for 10 s from
s 10"). Reproduce: `lab.py run space_allies --repeat 12 --lane N` (1 in 4 here); not fixed (outside the task of fix/w51-space2).

## Root cause
Observed (the recorded logs, `monitor.py replay`): in the galaxy view the mod's world is 0 and its trace lines say
`world=-` (trace::hex(0)). The monitor's OWN and PARTNER patterns took only hex worlds (`world=([0-9A-F]+)`), so every
own and partner line of the galaxy view was dropped without a word. `Together.check` then paired the guest's partner
lines of that stretch (same_place=0, which is right: the mod's `same_place()` is never 1 in world 0, puppets.cpp) with
the guest's nearest PARSED own line within 6 s: at s 10.0 the line of s 5.0 (80460035, before the zoom), at s 20.0 the
line of s 20.6 (80460035, back in the system). Two "together but seen elsewhere" lines 10 s apart are a violation
(together_s 20 - 10). Whether it fires depends on the timing of the zoom (the own line before it within 6 s of the
s 10 mark): 3 of 13 runs on fix/w51-space2. The old monitor also flags 2 of the 5 PASS runs of this branch below
(their logs, replayed), so the test fails on the old code as often as before.

## Fix
tools/tests/monitor.py: the patterns take `world=-` too, and `Together` counts world `-` as no place (`NOWHERE`), as the
mod does. No switch (lab tooling, not mod behaviour); the old monitor is `git show 76fab50f:tools/tests/monitor.py`.
`monitor.py report` over every recorded passed result before / after: one earlier together of campaign_space went
away, no new violation (with BUG-1147 one campaign_ct ground, see there).

## Verification
- FAIL (old monitor): fix/w51-space2 out/lab/20261115-042849 (space_allies#4), 20261115-044146 (#6), 20261115-044540
  (#1): "guest sees host elsewhere (same_place=0) for 10 s from s 10"; replayed with the old monitor the same, and the
  old monitor over this branch's PASS run 20261115-051528 flags #2 and #5 the same way.
- PASS (new monitor): out/lab/20261115-051528 (space_allies --repeat 5, lane 7, 5/5; every run went through the
  galaxy view, 2-3 own lines with world=-), out/lab/20261115-052922 (again 5/5 with the BUG-1147 monitor; the old monitor over its logs flags #4); replayed: the three FAIL runs above have no violation.
- A real separation still fails: gates_selftest `together_space_apart` (the same timings with the guest's own lines
  in the system) and the logs of 20261115-051528 space_allies#1 with the guest's partner lines set to same_place=0
  and its own lines to the host's system after the zoom: replay reports together for the guest (from s 20) and the
  host (from s 30); the galaxy stretch before stays quiet.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-tests (the monitor reads world=- lines, world - is no place for together).
