---
id: BUG-0831
title: Creature: the host keeps playing a departed guest's social game, a newcomer of the same id inherits it
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: host
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-creature
commits: 122a7f0b
tests: social_leave_host
signature:
docs: docs/re/creature.md#creature-c2-audit-0713
related: BUG-0422 BUG-0179
---

## Report
No player report. With 3-4 players, a guest that drops mid social game leaves the host's NPC playing prompts and a fail reaction for up to 60 s; a newcomer under the same id gets feed lines and a result for an NPC it never engaged.

## Root cause
Observed at runtime. g_games (keyed by player, NPC) had no leave handling: after the guest left, the host's Kook (1 prompt) waited out the prompt and played the fail reaction 0x53685D8F of that game (lab 20261112-073101, the game's current animation of the host's Kook); a newcomer of the id got the K_RESULT.

## Fix
g_games is a player_life state (PLAYER_STATE "social.games"): forget_all on every leave and join drops that player's games. `coop.players held <id>` lists it. Switch `coop.fix social.forget_leaver off`.

## Fix idea
Add social::on_player_leave(id) (or a PLAYER_STATE) that erases that player's g_games entries.

## How to test
Three players: guest B engages an NPC, B leaves; the host's prompts_sent must stop rising (fix) vs rises (main).

## Verification
Test `social_leave_host` (coop_creature_social_leave.py): a guest engages a Kook (real click), the host's game runs the game with its prompt, the guest leaves; for 30 s the host's Kook's animations (`coop.npc cur`): a prompt started again or the fail reaction fails the test.
* FAIL: out/lab/20261112-073101 (`SPORE_FIX_OFF=social.forget_leaver`, lane 6): the fail reaction 53685D8F.
* PASS: out/lab/20261112-073116 (lane 7): nothing started.
* PASS: out/lab/20261112-080216 (lane 6, the switch on; only nest.mirror_epoch off in that run): the second PASS.
* (First version of the test, out/lab/20261112-072244, counted the prompt still playing at the leave: changed to new starts only.)

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
- 2026-11-12 (fix/w48-creature, C2 of 0.7.13): fixed (social.forget_leaver); FAIL 20261112-073101, PASS 20261112-073116, 20261112-080216.
- 2026-11-12: released in 0.7.13.
