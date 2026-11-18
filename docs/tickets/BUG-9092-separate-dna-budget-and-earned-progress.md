---
id: BUG-9092
title: DNA spending incorrectly removes partner earned progress
status: fixed
kind: bug
stage: Creature
reported: 2026-11-07
source: User-approved selective upstream v0.7.9 ports
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in: unreleased
branch: fix/lineages-upstream-079
commits: d21ce60f
tests: tests/dna_sync_tests.cpp tools/tests/lineages_dna_progress.py
signature:
docs: docs/06-testing.md#testing-lineages-upstream-079
related:
---

## Report and root cause

Native AddEvolutionPoints changes both spendable DNA and cPlayer earned progression. The old guest reconciliation used it for an editor budget reduction. Native negative control: host budget/progress 60/0, guest 60/-40.

## Fix

Adapt upstream BUG-0630 rather than changing all negative dna_add_raw calls. Keep the existing shared death penalty unchanged. Add optional world-scoped earned progress to MSG_DNA; reconcile earned progress through native AddEvolutionPoints, then set the budget through native SetEvolutionPoints. This also handles gain plus spend in one update, duplicate totals and caught-up progress. Hold both fields when a guest is elsewhere. Same-build peers required for the new behavior; absent tails retain legacy behavior.

Upstream source: https://github.com/WoodyWizard/SporeCoop/blob/v0.7.9/docs/tickets/BUG-0630-creature-an-editor-s-dna-spend-reaches-the-partn.md

## Verification

FAIL and PASS evidence (targeted Windows runs, not a Linux lab run):

build/local/079-dna-fail.log: seven unit failures with the old path; 079-dna-pass.log: zero. 079-dna-native-fail.log: reproduced editor-spend regression in the game. 079-dna-native-pass.log: spending leaves both at 60/0; gain yields 70/10; penalty debit yields 63/3; periodic resend and fault checks pass. The native test exercises the editor result setter and penalty debit, not editor UI or the full death timer.

## History

2026-11-07: selective port and targeted regression checks. No release published.

