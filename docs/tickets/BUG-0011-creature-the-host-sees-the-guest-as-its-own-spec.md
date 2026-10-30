---
id: BUG-0011
title: Creature: the host sees the guest as its own species / not its changes
status: not-reproduced
kind: desync
stage: Creature
reported: 2026-10-25
source: player screenshots
role: host
versions: 0.7.2.1
fixed_in: 
branch: fix/creature-desync
commits: 
tests: creature_desync
signature: 
docs: docs/known-issues.md
related:
---

## Report
"He is my species from my view", "I don't even see his changes".

## Root cause
Not reproduced on 0.7.2.2 after editors, rejoins and reloads. Two 0.7.2.1 bugs explain it and are fixed in 0.7.2.2 (BUG-0004: creations sent without records, re-apply failing on rejoin).

## Fix
None separately.

## Verification
The test checks the host shows the guest's edited creature after the editors and after a reload.

## History
- 2026-10-25: reported (player screenshots).
- 2026-10-25: not reproduced, see Root cause.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-30: fix/w13-creature: tried again: creature_desync (editors, a crash and a reload, the host shows the guest's edited creature) and nest_species OK in out/lab/20261030-141545 (fix/w13-creature, lane 5, GOG 2024); legless_guest_cancel OK (out/lab/20261030-142629, both screens show the right species after editors and a rejoin). Not reproduced; stays.
