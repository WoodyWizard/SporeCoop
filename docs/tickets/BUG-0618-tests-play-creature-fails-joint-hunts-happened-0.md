---
id: BUG-0618
title: tests: play_creature fails 'joint hunts happened (+0 joint hits)' when the guest bot lands few hits (16 of 196 attacks)
status: open
kind: test
stage: Creature
reported: 2026-11-06
source: rel/0.7.9 integration run out/lab/20261106-040221 (fix/w45-int2)
role: 
versions: 
fixed_in:
branch: fix/w45-int2
commits:
tests: play_creature
signature:
docs:
related: BUG-0099
---

## Report
rel/0.7.9 integration run out/lab/20261106-040221 (lane 3): play_creature FAIL "joint hunts happened (+0 joint hits)".
Both bots hunted the same NPCs (10 kills seen by both), the guest bot pressed its attack keys 196 times, the host
applied 16 guest hits. A joint hit (teamplay.cpp, BUG-0099) needs a hit of the host and one of a partner on one
target within kHuntWindow = 6 s.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (counts over 14 play_creature runs, 2026-11-05/06): the number of guest hits the host applies varies
10-50 per 10-minute run and the joint hits 0-26 with it (rel/0.7.8 20261105-155603: 44 / +14; the first rel/0.7.9
integration run 20261106-013035, before the C2, L1 and P3 follow-up merges: 15 / +4; 20261106-040221: 16 / +0;
fix/w45-int2 on rel/0.7.9: 45 / +3 (051910), 50 / +26 (053225), 39 / +3 (054817)). Low guest hit counts happen
before the merges too, and no merged change touches the hit path (teamplay, MSG_DAMAGE_REQ, the damage filter):
not a regression. In 054817 every hit the guest's game sent arrived (sent 39, applied 39), so nothing is lost on
the way: the guest bot lands few of its key presses (39 of 282 attacks). Inferred, not checked: the bot presses an
ability key 0.2-0.5 s after its click on the target, often while its creature still walks to it (a joined hunt
starts 17-74 m away), and an ability out of reach does nothing.

## Fix
No game change, no looser limit. Diagnostics: coop.team prints "hits host N, partners M" (the hits the joint table
counted), play_creature prints "hits: guest sent / host applied; joint table: host / partners", so the next failure
shows whether the guest's hits did not land, were lost, or did not overlap with the host's.

## Verification
no-fail-run: a flaky bot outcome, not reproduced in 3 runs on lane 3 (051910, 053225, 054817, all PASS).

## History
- 2026-11-06: opened.
- 2026-11-06 (fix/w45-int2): analysed, diagnostics added; open as a test flake (the bot's landing rate).
