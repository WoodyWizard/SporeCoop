---
id: BUG-1150
title: E1: a guest's kept rejoin token expires after a day, so the next evening of a weekly game it is a new player to the host
status: fixed
kind: bug
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: 1bb53ffb
tests: store_tests (make test)
signature:
docs: docs/01-architecture.md#arch-player-store
related: BUG-1135, BUG-1149
switch: net.rejoin_month
---

## Report
Found while building E1 (BUG-1149): the store on the host's disk finds a player by its rejoin token, and the clean leave kept that token on the guest's disk for a day only (BUG-1135): the next evening of a weekly game the guest came with a fresh token and was a new player.

## Root cause
Observed in code: rejoin_kept.h kTtl = 24 h (find / expire).

## Fix
KeptFile::ttl (rejoin_kept.h): 30 days with `coop.fix net.rejoin_month` (rejoin.cpp sets it), a day with it off (0.7.15 before E1). Still per host address and name, at most 16 entries, salted.

## Verification
* unit: store_tests test_token_month: a token kept a week ago is found with the switch on (PASS) and not with
  it off (the FAIL side: the next week's join gets a fresh token).
* no-fail-run: a lab run cannot wait a week; the restart of the same evening is space_session (PASS runs of
  BUG-1149).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store: 30 days.
