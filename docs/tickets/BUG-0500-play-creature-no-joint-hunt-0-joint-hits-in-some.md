---
id: BUG-0500
title: play_creature: no joint hunt (+0 joint hits) in some runs, and the host's own creature 0.71 m under the ground (monitor rule ground), with the relay off
status: fixed
kind: test
stage: Creature
reported: 2026-11-03
source: BUG-0471 A/B, lane 4, 2026-11-03
role: 
versions: 
fixed_in: 0.7.4
branch: fix/w40-play
commits: 5016c091
tests: play_creature
signature:
docs: docs/re/creature.md#creature-chat-under-caption
related: BUG-0471, BUG-0499, BUG-0482, BUG-0504
---

## Report
With the effect relay switched off (SPORE_FIX_OFF=creature.effects_relay, fix/w40-effects, lane 4,
out/lab/20261103-124913 in SporeCoop-wt/fix/w40-effects), play_creature#2 and #4 failed `joint hunts
happened (+0 joint hits)`; #2 also failed the lab's monitor gate `ground: host: its own creature 0.71
m under the ground (Creature) on 2 lines from s 290`. main had the joint-hunt failure too
(out/lab/20261103-012624 play_creature#1 in the main checkout). Not the relay's (it was off).

## Root cause
Observed (124913#2 logs): from s 290 to the end of the run both games had `busy=cinematic,input_lock,paused`
on every trace line, the bots' attacks and kills stopped (151 / 139 attacks, 8 kills from s 310 to 558), the
joint hits stayed 0. The DNA had just passed 100 (78 -> 103): the brain upgrade plays cinematic F6A4B1E1 with
the "Brain power! ..." caption, which waits for Enter / OK on each game (lane 6 probe). play_creature's bots
answered only the death caption, so they stood in this one for minutes: no joint hunt (a test bot gap, not a
game bug; a player presses OK). The same caption's input lock lost the chat lines (BUG-0482).
The own creatures 0.63-0.71 m under the ground during that caption: not reproduced at the fixture's nest,
split into BUG-0504.
## Fix
play_creature.py: the bots answer the "Brain power!" caption with Enter like a player (DIALOGS, counted as
`caption_ok`). Test-only change, no switch.

## Verification
no-fail-run: test bot change (the caption was found in the failing runs' logs: out/lab/20261103-124913
play_creature#2, 20261103-012624 #1/#2); the final play_creature x3 of fix/w40-play shows whether the joint
hunt now happens: out/lab/20261103-160342 play_creature 3 of 3 OK, joint hits +3 / +7 / +10, the caption
answered once per run (`caption_ok=1`), no chat line lost.

## History
- 2026-11-03: opened.
- 2026-11-03: fixed on fix/w40-play (the bots answer the brain upgrade caption); the ground part split into BUG-0504.
- 2026-11-03: released in 0.7.4.
