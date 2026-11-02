---
id: BUG-0447
title: vote_net_4p: the leaver's action vote ran 0 times (its result waited at the busy gate after the check)
status: fixed
kind: test
stage: Creature
reported: 2026-11-01
source: lab 20261101-210646
role: host
versions: main f0cefbaa (0.7.3.4 rc)
fixed_in: 0.7.3.4
branch: fix/w37-vote-leaver
commits: 8ca6a86b, 29074770
tests: vote_net_4p, party, vote
signature:
docs: docs/06-testing.md#testing-busy-gate
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

The same class in `vote` (coop_vote.py), lab 20261101-223733 (lane 6): `after the evolution the guest still
plays its own creature` FAIL ("avatar 065CBE70", own 290225CD). Observed: host `Editor -> Creature` at s 40.0,
the world share after the evolution waited for the evolution's cinematic (BUG-0404 kShareGate) and went out
at s 50.1; the guest reloaded (Creature -> Galaxy menu at s 56.8, Loading -> Creature at s 61.0, the
world's species first) and took its own body at s 64.0 (`own avatar species 065CBE70 -> 290225CD`, the
planned swap 3 s after a load). The test checked once after a fixed 20 s from "evolved the species", inside
that reload.

## Fix
Test only, no product change (so no switch): `coop_party.ran_once()` waits for the result to run (up to
the rest of the 10 s window after the leave, 10 s elsewhere), then 2 s more so a second run shows too.
vote_net_4p ([leaver], [all]) and coop_party.py ([vote], [join in a vote]) use it; vote_net_4p prints
the host's `the vote's action ...` lines of the leaver step. coop_vote.py waits up to 60 s more for the
guest's avatar to be its own creation (a guest that keeps the world's species still fails).

## Verification
FAIL (the old check, gate on): 20261101-210646 (main), 20261101-223011 (`[leaver] it ran once (0 -> 0)`,
the action waited for `cinematic,input_lock` 1.75 s). The old check PASSES with the gate off
(`SPORE_FIX_OFF=core.vote_action_busy`): 20261101-223032 (the action ran 6 ms after the vote's end), which
shows the check only held while the result ran at once. PASS (the new check, gate on): 20261101-223718 #1
(waited for `cinematic,input_lock`), #2 (waited for `paused`; the run failed on BUG-0450, another check),
20261101-224307 #1 and #2 (waited for `paused`): 4 of 4 leaver steps ran once within 10 s.
`vote`: FAIL 20261101-223733 (the fixed 20 s read, the reload 21 s after the evolution); PASS 20261101-225404
#1-#3 (the guest's own body 0, 0 and 4 s after the old read).

## History
- 2026-11-01: opened.
- 2026-11-01: fixed on fix/w37-vote-leaver (test / judge only).
- 2026-11-02: released in 0.7.3.4.
