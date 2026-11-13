---
id: BUG-1044
title: fields: PLAYER_STATE("fields") erases and counts g_stats by player id, but g_stats is keyed by kind id (diagnostics)
status: fixed
kind: test
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-8
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-small
commits: f4aca716
tests: tribe_zero_fields
signature:
docs: docs/06-testing.md#testing-t1-small
related: BUG-0422
---

## Report
Audit SPC-8 (second half). Not seen at runtime, found by code reading. Diagnostics only: when a guest with player id 1..3 leaves, the host's fields statistics of kind 1..3 (K_NPC, K_TRIBE_MEMBER, K_UFO) are erased, and the departed-player state check counts a kind's stats as state left behind for the player with the same number.

## Root cause
Confirmed at runtime: PLAYER_STATE("fields") erased g_stats[id] (keyed by kind) on a leave of player id: after the guest
(id 1) left, the host's statistics of kind 1 (npc) went 87 -> 0 and `coop.players held 1` listed `fields 1`.

## Fix idea
Drop g_stats from PLAYER_STATE (it is not per player); only g_sent is per player. No switch needed (diagnostics).

## How to test
A state-leak check after a guest leaves (the PLAYER_STATE sum used by the BUG-0422 test, `coop.state` or the existing player-leave test): with a session that relayed NPC fields, the departed id 1 must report 0 entries for `fields`; main reports 1 (FAIL).

## Fix
The player state of fields forgets and counts only g_sent (per player). Switch `coop.fix fields.player_state off`
(diagnostics only; 0.7.13).

## Verification
Test `tribe_zero_fields`, its last step (the host's `coop.fields` npc statistics across the guest's leave).
FAIL (SPORE_FIX_OFF=fields.player_state): out/lab/20261113-031337 (87 -> 0, held `fields 1`). PASS: out/lab/20261113-030613,
out/lab/20261113-031904 (kept, `nothing held`).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
