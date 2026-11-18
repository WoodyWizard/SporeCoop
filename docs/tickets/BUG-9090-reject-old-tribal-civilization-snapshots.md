---
id: BUG-9090
title: Reject late Tribal and Civilization snapshots
status: fixed
kind: bug
stage: Tribe Civilization
reported: 2026-11-07
source: User-approved selective upstream v0.7.9 ports
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in: unreleased
branch: fix/lineages-upstream-079
commits: d21ce60f
tests: tests/validate_tests.cpp
signature:
docs: docs/06-testing.md#testing-lineages-upstream-079
related:
---

## Report and root cause

Both guest receivers accepted every cycle different from the current one. A delayed older cycle could overwrite newer units and discard accumulated cleanup membership.

## Fix

Port v0.7.9 CycleTracker, including 16-bit wrap ordering and chunk bits. Reject before changing guest state. Reset at session/world teardown so a new host sequence is accepted.

Upstream source: https://github.com/WoodyWizard/SporeCoop/blob/v0.7.9/docs/tickets/BUG-0616-tribe-civ-a-late-chunk-of-an-older-snapshot-cycl.md

## Verification

FAIL and PASS evidence (targeted Windows runs, not a Linux lab run):

build/local/079-cycle-fail.log: old ordering fails !late and completion assertions. build/local/079-cycle-pass.log: complete validation suite passes. Native Tribal chief regression passes (079-tribe-native.log). Deliberately reordered live packets and Civilization gameplay have not been tested in this batch.

## History

2026-11-07: selective port and targeted regression checks. No release published.

