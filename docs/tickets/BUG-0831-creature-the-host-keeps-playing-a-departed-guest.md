---
id: BUG-0831
title: Creature: the host keeps playing a departed guest's social game, a newcomer of the same id inherits it
status: open
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0422 BUG-0179
---

## Report
No player report. With 3-4 players, a guest that drops mid social game leaves the host's NPC playing prompts and a fail reaction for up to 60 s; a newcomer under the same id gets feed lines and a result for an NPC it never engaged.

## Root cause
Observed in the code (line numbers of main e49e1e23 = 0.7.11.1): g_games is keyed by (player id, NPC id) (social.cpp:712); social.cpp has no on_player_leave and no PLAYER_STATE; host_prompt_tick (883-919) keeps sending K_PROMPT for kEngageTime (60 s), then fail_game broadcasts K_RESULT. Confidence: low-medium (small impact).

## Fix
Not fixed yet.

## Fix idea
Add social::on_player_leave(id) (or a PLAYER_STATE) that erases that player's g_games entries.

## How to test
Three players: guest B engages an NPC, B leaves; the host's prompts_sent must stop rising (fix) vs rises (main).

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
