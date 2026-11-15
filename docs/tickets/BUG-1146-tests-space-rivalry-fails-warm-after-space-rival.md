---
id: BUG-1146
title: Tests: space_rivalry fails warm after space_rivalry (the rivalry hit counters of the run before are not reset by the warm reset)
status: fixed
kind: test
stage: Space
reported: 2026-11-15
source: test
role: 
versions: 
fixed_in: unreleased
branch: fix/w51-tests
commits: 7e63b1a9
tests: space_rivalry (warm after space_rivalry)
signature:
docs: docs/06-testing.md#testing-monitor-nowhere, docs/archive/known-issues-2026.md#issue-rivalry-warm-counters
related:
---

## Report
`lab.py run space_rivalry,space_trade --in-order --repeat 2` on fix/w51-space2 (out/lab/20261115-042847, lane 6):
space_rivalry#1 (cold) PASS, space_rivalry#2 (warm after it) FAIL "neutral players cannot hurt each other" and "a
lethal PvP hit destroys the guest's ship". The guest's `coop.space rivalry` line printed before the test's first shot
already says hits_got=43 dmg_got=1500.0 pvp_deaths=1 (the PvP phase of the run before); the check wants hits_got == 0.
Inferred: the rivalry counters live in the game process and the warm reset does not clear them (a cold start does);
the test compares absolute counters. Fix idea: compare with the values at the test's start, or clear the counters in
the warm reset. Not a mod regression; not fixed here (outside BUG-1139).

## Root cause
Observed: `g_hits_sent`, `g_hits_got`, `g_dmg_got`, `g_pvp_deaths` (and splits / merges / puppet_hits / restored ...)
of mods/coop/space_rivalry.cpp are static counters of the game process; `session_end()` and the lifecycle registry
do not touch them, so they count since the game started. A warm reset keeps the process: in out/lab/20261115-051639
the guest's hits_got at the start of the neutral check was 0, 43, 86 in the three runs. The test compared absolute
values (`hits_got == 0`, `pvp_deaths == 1`).
Decision: the counters are diagnostics only. They are printed by the console command `coop.space rivalry` and by
nothing else (no Space page row, no session report, no message): no player sees them, and nothing in the mod decides
anything by them. So they are no SESSION_STATE (a reset of them would change no player-visible behaviour, so no
FAIL/PASS twin of a mod switch is possible or needed); the test reads deltas, as coop_space_rival_away already does
with `restored`.

## Fix
tools/tests/coop_space_rivalry.py: the rivalry status at the start of the neutral phase is the base; "neutral players
cannot hurt each other" wants hits_got + 0, "the host's hits damage" hits_got over the base, "a lethal PvP hit"
pvp_deaths + 1. No mod change.

## Verification
- FAIL (old test, warm after space_rivalry): fix/w51-space2 out/lab/20261115-042847 space_rivalry#2: "neutral players
  cannot hurt each other (shot (hits_sent=43))", "a lethal PvP hit destroys the guest's ship" (hits_got=43,
  pvp_deaths=1 from #1).
- PASS: out/lab/20261115-051639 (`lab.py run space_rivalry --repeat 3 --until all --in-order`, lane 6): #1 cold,
  #2 and #3 warm after space_rivalry ("warm reset 16 s"), all OK; the bases were hits_got 0 / 43 / 86.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on fix/w51-tests (the test reads the counters as deltas; they are diagnostics no player sees).
