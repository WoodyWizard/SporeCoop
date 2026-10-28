---
id: BUG-0022
title: editor_strays fails once under a full lab load
status: open
kind: test
stage: Creature
reported: 2026-10-25
source: lab run 20261025-145956
role: host
versions: 0.7.2.4
fixed_in: 
branch: 
commits: 
tests: editor_strays
signature: 
docs: docs/06-testing.md#testing-editor-strays-load
related: BUG-0212
---

## Report
Two creatures of the guest's species left in one herd after evolution 2 (test and its retry) in a 32-test run.

## Root cause
Unknown; alone it passed 3/3.

## Fix
None.

## Verification


## History
- 2026-10-25: reported (lab run 20261025-145956).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-28: fix/w4-tests (wave 4): not reproduced alone; a full-load reproduction needs 6 busy lanes, which a wave agent with one lane cannot make. Left open.
- 2026-10-28: fix/rel-strays: likely the same timing as BUG-0212 (the check ran while the host's evolution cinematic still held the old puppet's deferred destroy; a loaded lab makes the cinematic and the editor exit slower). The test now waits for the cinematic and the share; left open until a full lab run passes.
