---
id: BUG-0267
title: A lab game hung at boot after 'mod coop: cannot copy coop.dll (err 32)'
status: fixed
kind: tooling
stage: 
reported: 2026-10-29
source: lab 20261029-070811 campaign
role: 
versions: 
fixed_in: unreleased
branch: fix/w6-campaign
commits: dcc2f3a
tests: campaign
signature:
docs:
related:
---

## Report
campaign#3 of lab 20261029-070811 (lane 7, cold, three 4-player campaigns starting at once) died
in twoinst.start: `game.mode` timed out. The host's log: "mod coop: cannot copy ...coop.dll
(err 32)", then nothing.

## Root cause
The host did not answer within 90 s (a busy machine: three lanes starting 4 games each), and
twoinst started a second copy of the same instance next to it. The first one still held its shadow
copy coop.i13.1.dll: the copy failed with ERROR_SHARING_VIOLATION and the second game hung too.

## Fix
twoinst._start: an instance that did not answer in 90 s ends the lane's games (spore.sh kill) and
both start again, never a second copy of a running instance.
switch: none (test tooling)

## Verification
no-fail-run: harness race seen once under load; the next final campaign run checks it.

## History
- 2026-10-29: opened.
- 2026-10-29: fixed on fix/w6-campaign.
