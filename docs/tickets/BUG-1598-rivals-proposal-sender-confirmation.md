---
id: BUG-1598
title: Rivals: sender of outgoing Alliance or Peace proposal receives no pending confirmation feedback
status: fixed
kind: bug
stage: All
reported: 2026-11-26
source: AI playtest (v0.8.2.3)
role: both
versions: 0.8.2.3
fixed_in: 0.8.2.4
branch: main
commits: 225ccb98
tests: make test
signature: clicking Propose Alliance or Propose Peace gives no immediate confirmation to the sender
switch: none (status notification in rivals.cpp)
docs:
related:
---

## Report
When a player sent an Alliance or Peace proposal in F9 -> Rivals, the sender got no immediate pending confirmation message.

## Root cause
`rivals.cpp` notified the recipient of the incoming proposal without flashing a confirmation to the proposing player.

## Fix
Added clear pending confirmation feedback to the sender of outgoing Alliance and Peace proposals in `mods/coop/rivals.cpp`.

## Verification
- no-fail-run: UI/feedback confirmation verified by `make test`.
