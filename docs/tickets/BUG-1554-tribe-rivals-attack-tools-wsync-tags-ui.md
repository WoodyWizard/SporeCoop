---
id: BUG-1554
title: Tribe Rivals: guest War attack, guest tool purchases, wsync isolation, tags/relations, Creature payout and NPC Influence UI
status: open
kind: bug
stage: Tribe
reported: 2026-11-25
source: v0.8.0 independent audit
role: both
versions: 0.8.0
fixed_in:
branch: w0801/tribe
commits:
tests:
signature:
docs: docs/11-rivals.md
related: BUG-1548, BUG-1547
---

## Report
Independent static audit of `BUG-1548` found critical gameplay blockers in Tribe Rivals:
1. `tribe_host.cpp:706` runs `ff_try_order` in Tribe Rivals, swallowing guest attack orders against host tribe members at War.
2. Guest tool/hut purchases (`worldobj.cpp:576-585`, `tribe.cpp:1642`) still target host `player_tribe()` instead of sender's tribe.
3. `wsync.cpp` clobbers guest hut level/pop cap (`s_tlevel`) and goal bar (`s_goal`) with host values, omits host village food sync, `tribe_host.cpp:712` feeds guest actions into host `wsync::goal_target`, and `wsync.cpp:928` (`s_nest`) lacks a `creature_rivals::active()` guard.
4. Missing rival member/chief tags (`tribe.cpp:5167-5177`), missing player-vs-player War/Ally write into `cRelationshipManager`, co-op `epic_tick` still active in Rivals (`tribe.cpp:4393-4414`), Creature Rivals draft payout never delivered as Tribe food, and no NPC tribe list for F9 Influence UI.

## Root cause
See `docs/plan-0.8.0.1.md` WP-4.

## Fix
TBD.

## Verification
TBD.
