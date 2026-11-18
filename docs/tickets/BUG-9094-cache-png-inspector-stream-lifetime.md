---
id: BUG-9094
title: PNG inspector releases a stack FileStream
status: fixed
kind: crash
stage: Menu
reported: 2026-11-07
source: Isolated Lineages Cache inspection probe
role: host guest
versions: 0.7.3.6-lineages26-dev
fixed_in: unreleased
branch: feat/lineages-cache
commits: 8bd7ca16
tests: tools/tests/lineages_library.py
signature: 3010a413:SporeApp.exe+0x527A11
docs: docs/re/creations.md#creations-cache-native-feed
related: BUG-9093
---

## Report and root cause

Read-only inspection of a valid PNG terminated isolated instance 4. Native
exception report records a write to 2F at live 647A11, heap code, after DecodePNG.
The probe copied the native importer's FileStream construction but omitted
its explicit AddRef at preferred 5FC405. The decoder owns a temporary smart
reference. Releasing an initially unpinned stack stream tries to delete it.

## Fix and verification

Mirror native AddRef before decoding; destroy the pinned stack stream directly
after decoder metadata/stream cleanup. Two repeated native inspections passed,
followed by automatic scans, repaired PNG exports, eleven native imports and
two-profile reconnect/offline checks without faults. PASS evidence:
out/cache-probe/release-native.log and out/cache-probe/final-restart.log.
FAIL: out/cache-probe/inspect-crash (game exception report, not a lab run).
The triage utility does not parse the game's own exception format; build and
base verified against the paired QA process and report module table.

## History

2026-11-07: found during library export validation, before automatic use.
