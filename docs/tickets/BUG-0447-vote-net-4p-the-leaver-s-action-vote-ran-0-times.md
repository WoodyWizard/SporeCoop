---
id: BUG-0447
title: vote_net_4p: the leaver's action vote ran 0 times (its result waited at the busy gate after the check)
status: open
kind: test
stage: Creature
reported: 2026-11-01
source: lab 20261101-210646
role: host
versions: main f0cefbaa (0.7.3.4 rc)
fixed_in:
branch:
commits:
tests: vote_net_4p, party
signature:
docs:
related: BUG-0404
---

## Report
vote_net_4p in the final multi run 20261101-210646 (lane 3) failed one check: `[leaver] it ran once (0 -> 0)`.
The step: an action vote (`coop.vote action`) while Dave is still in his editor, Bob and Carol ready, Dave
leaves; the vote passed 0.5 s after the leave (`the vote passes within 10 s` ok), and the test read the
host's `actions run` counter right then: 0. Every other check passed, also step 4 (`1 -> 2`: the leaver's
action had run by then). vote_net_4p passed before main merged fix/w34-vote-cinematic (BUG-0404).

## Root cause
Observed (the host's log of that run): Bob and Carol came back from their editors at s 72.6; the world
share to the three guests waited for the stage's cinematic (`share_wait=cinematic`) and saved at s 82.7
(`perf: hitch 699 ms (Creature, saving, vote)`, `the game saved box is up 1706 ms after the save: confirming
it` at s 84.4, written at s 85.2). Dave left at s 85.831; `vote: end: everybody is ready, ready 3/3` at
s 85.835; `the vote's action waits: the game is busy (paused)`; `the test vote action ran` at s 86.777.
So the action ran once, on the host, 0.94 s after the vote ended: the busy gate of BUG-0404
(`core.vote_action_busy`, busy::kActionGate) held it while the host's game time stood. The leaver is not
part of it: the vote counted Dave out correctly and nothing was dropped.
Inferred: the pause was the game's own "game saved" box of the mod's world share (the save had just been
written, the box is confirmed by the mod); the trace has no line inside that second to show `busy=paused`.
The test read the counter at the instant the vote ended, which only held while the result ran at once
(0.7.3.3, or the gate off). The host is the machine that acts for an action vote, so the gate waited for
the right machine. coop_party.py (suite `party`) had the same instant read in two places.

## Fix
Test only, no product change (so no switch): `coop_party.ran_once()` waits for the result to run (up to
the rest of the 10 s window after the leave, 10 s elsewhere), then 2 s more so a second run shows too.
vote_net_4p ([leaver], [all]) and coop_party.py ([vote], [join in a vote]) use it; vote_net_4p prints
the host's `the vote's action ...` lines of the leaver step.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
