---
id: BUG-0223
title: a single frame without a player city wipes the host's vehicle and city ownership in the middle of a game
status: open
kind: desync
stage: Civ
reported: 2026-10-28
source: GitHub #157 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
From GitHub issue #157: https://github.com/WoodyWizard/SporeCoop/issues/157 (the full analysis, links and the scenario are there).

**Summary**
Civ decides that the world changed when the stage adapter's local entity is null for a frame. That entity is only an anchor built from the player civilization's first city. When it is null for one frame in the middle of a game, `civ::on_world_change()` runs on the host and throws away the vehicle table, every city's owner (captures and gifts included) and the running speed boosts, while the world is still there. Severity: low. No trigger has been seen, but the result for players is large.

This was left open by BUG-0059 (ba9dc46, release 0.7.3.0), which added `civ::on_world_change()` behind that trigger. The same commit range also added reads of vehicle memory before the alive check in `host_tick()` (BUG-0031 7f066e4, BUG-0059 ba9dc46).

**Where**
- The trigger fires on any cha

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-28: opened.
