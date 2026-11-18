---
id: BUG-9091
title: Arrival placement must leave moved chiefs alone
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-07
source: User-approved selective upstream v0.7.9 ports
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in: unreleased
branch: fix/lineages-upstream-079
commits: d21ce60f
tests: tests/chief_arrival_tests.cpp tools/tests/duo_tribe_native_chiefs.py
signature:
docs: docs/06-testing.md#testing-lineages-upstream-079
related:
---

## Report and root cause

The inherited arrival placement scans for 60 seconds and can reposition a chief that walks or stops near another chief.

## Fix

Adapt v0.7.9 BUG-0610: exempt any chief observed moving at least 0.3 m/s or more than 1.5 m from its initial sampled location for the whole arrival window. The portable Movement state also remembers a return to the origin.

Upstream source: https://github.com/WoodyWizard/SporeCoop/blob/v0.7.9/docs/tickets/BUG-0610-tribe-a-host-member-walking-at-9-m-s-is-put-21-m.md

## Verification

FAIL and PASS evidence (targeted Windows runs, not a Linux lab run):

build/local/079-chief-fail.log: legacy placement fails four movement cases. 079-chief-pass.log: all pass. 079-tribe-native.log: both instances agree on native chief/staff, absence and replacement, zero faults. No live teleport reproduction; BUG-9062 remains open and is not attributed to this cause.

## History

2026-11-07: selective port and targeted regression checks. No release published.

