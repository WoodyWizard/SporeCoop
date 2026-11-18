---
id: BUG-9236
title: Network: game events wait less behind a world transfer (window sized from the link's measured rate)
status: fixed
kind: feature
stage: all
reported: 2026-11-15
source: owner 2026-11-15 (4P A, step 4)
role: all
versions: 0.7.14.1-lineages60-dev
fixed_in: 0.7.14.1-lineages61-dev
branch: feat/lineages-47
commits: 809bb049
tests: manual Windows QA: the MSI two games, the net traffic line (A/B by the switch)
signature:
docs:
related:
---

## Report
Owner 2026-11-15 (4P A step 4): BigTexas saw stutters; the code survey found a world transfer's chunks sharing the reliable queue with the game's own reliable messages.

## Root cause
Observed in the code (world.cpp): up to 160 chunks (rtt >= 40 ms) were queued ahead of any later reliable message (spawns, damage, effects); on a 125 KB/s link that is over a second of wait.

## Fix
- The window is the link's measured rate (BUG-9098 pace_rate_, packets per ms) times the round trip + 0.2 s, 24..160 chunks, so a game message waits about that long at most. Switch world.window_by_rate.

## Verification
no-fail-run: not measured: the test PCs' link has a round trip under 40 ms, where the window is unchanged (600). Built; world transfers on the MSI complete.

## History
- 2026-11-15: opened.
- 2026-11-15: done (4P A).
- 2026-11-16: released in 0.7.14.1-lineages61-dev.
