---
id: BUG-9095
title: Provisional Creature library PNG still contains Cell model
status: fixed
kind: bug
stage: Creature
reported: 2026-11-07
source: Isolated authored-library export test
role: host guest
versions: 0.7.3.6-lineages26-dev
fixed_in: unreleased
branch: feat/lineages-cache
commits: 8bd7ca16
tests: tools/tests/lineages_library.py
signature:
docs: docs/re/creations.md#creations-cache-native-feed
related: BUG-9093
---

## Report and root cause

The Cell-to-Creature landfall catalog copies its provisional thumbnail from
the cell. Its picture also contains the old Cell model. Exporting this PNG
unchanged silently imports a Cell instead of the current Creature.

## Fix and verification

Inspect embedded metadata and compare its group with the authored record.
For a mismatch, encode the current EditorSaves model and metadata into a
separate cached PNG using the existing picture. Original editor records,
Documents creations and campaign files remain untouched. Cache the result
by source PNG/metadata/BEM digest.

FAIL: native inspection decoded group 40616200 for Creature record 40626200.
PASS: repaired PNG decodes 40626200, retaining native name and author. Both
clean QA scans report repaired=1/export_failed=0. The two-instance test imports
the repaired Creature alongside original outfitted creatures, ships, vehicles
and buildings. Evidence: out/cache-probe/release-native.log, release/received.json.

## History

2026-11-07: found during library validation; fixed only the cache export path.
The provisional source picture is intentionally retained.
