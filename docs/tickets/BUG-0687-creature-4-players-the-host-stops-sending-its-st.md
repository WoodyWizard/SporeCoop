---
id: BUG-0687
title: Creature, 4 players: the host stops sending its state when a guest rejoins, its share saves for that guest are never written and it freezes about 30 s later
status: open
kind: freeze
stage: creature
reported: 2026-11-07
source: lab
role: host
versions: 0.7.10
fixed_in:
branch:
commits:
tests:
signature: host log ends with "world: no saved box and no file written 10013 ms after the save for player 1: saving again (try 2/3)"; FROZE (no frame for 10 s)
docs: docs/known-issues.md#issue-creature-rejoin-share-freeze
related: BUG-0686
---

## Report
campaign_tribe on lab lane 3, rel/0.7.10 4c1d891f (lab 20261107-182231, run #2, campaign
out/lane3/campaign/20261107-183105 of the rel/0.7.10 worktree): "the campaign stopped in creature (wanted tribe)".
In Creature play the chaos step made Bob leave and join again (s 86 -> s 93 of the host log). From then on:
* the host's trace lines (every 10 s) stop after s 80; the guests' monitors see the host's state age grow from
  about s 86 ("partner 0 (Alice) ... hidden: state older than 5 s" from s 100, "link unhealthy, state 13.7 s old");
* "sending the world to Bob", "world: saving for the share of player 1" at s 93; no "game saved" box and no file
  10 s later: "saving again (try 2)" at s 103, "(try 3)" at s 114, a third post at s 115, then the host's log ends;
* the relay to the other guests stops at about s 121 (Bob's log: Carol and Dave "not responding" at s 126);
* the console times out at +246 s, the watchdog reports FROZE (no frame for 10 s) at +266 s.
The host still ran frames between s 86 and s 115 (DNA and save retry lines), so the main loop was alive while it
sent no state.

## Root cause
Not analysed (found by BUG-0686's agent; not part of that task). The only campaign host log of all worktrees with
"saving again (try 3)" (grep on 2026-11-07); nothing merged after e7d1734c touches the Creature share path (inferred: not a 0.7.10 regression).
To reproduce: `SPORE_LANE=3 python3 tools/tests/coop_campaign.py --from creature --to tribe --minutes 2.0 --seed
953406079` (the seed of the run; the rejoin is the chaos step).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-07: opened (fix/w46-advance-reg): one lab run, seen once.
