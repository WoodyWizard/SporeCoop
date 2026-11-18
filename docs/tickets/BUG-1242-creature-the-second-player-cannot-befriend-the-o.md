---
id: BUG-1242
title: Creature: the second player cannot befriend the other creatures (social game)
status: open
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video
role: 
versions: <0.7.15
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
The owner saw it in a players' video (a version before 0.7.15): in the Creature stage the second player (the guest)
plays the social game with an animal (sing / charm / dance / pose to make it an ally) and it never succeeds; the host
befriends animals normally. Expected: every player befriends creatures as in single player, with the same result
(an impressed animal, an allied nest) in both games.

## Root cause
Observed (Ghidra + the game's data, docs/re/creature.md#creature-social-kinds): a creature ability's +0x10 (property
0x0743BDD6, cCreatureAbility::Parse 0x4D9310) is its social KIND: 1 sing (type 0x25), 2 charm (0x22), 3 dance (0x24),
4 pose (0x23), 0 any other ability. The docs (8.3, #creature-social-prompts) read it as "1 on the social ability", and
mods/coop/social.cpp took `+0x10 == 1` as "a social ability" on the guest: the AbilityBar_Use hook (h_BarUse) passed a
click on charm / dance / pose to the game, which does nothing without its own NPC session, and the key poll mapped the
keys 2-4 to "the second / third / fourth ability of kind 1" (none). The host's side was right: it picks the NPC's prompts
from its species' candidates (any kind) and counts an answer by kind (the native 0xD9B070 rule: another kind gives
0.1 of the points). So a guest could answer only by singing: an animal whose prompt list holds a charm, dance or pose
(most nests after the first ones) could not be befriended, the game failed after its list.
Observed (runtime, `coop.social herds`): the `creature` fixture's nests prompt only to sing, so every social test
passed; in the campaign's world (`creature_cadian`) the nests from about 175 m on prompt to charm / dance / pose.
The host plays natively (its own session): not affected.

## Fix
social.cpp `social.kinds`: on a guest every social kind 1-4 is a social ability for the AbilityBar_Use hook (a click on
the bar's charm / dance / pose button, or the game's own key path, plays it on the avatar and sends it to the host as
the sing did), and the key k (1-4) in the social stance plays the ability of kind k (the bar's social column k-1,
AbilityBar_Column 0xD48FA0). No wire change (K_ACT carried the type already). Switch `coop.fix social.kinds off`: only
kind 1 (0.7.15). Test commands: `coop.social herds` (each nest herd's prompt kinds), `coop.social bar` (the HUD bar's
slots, their kinds and button windows); `coop.social` (guest) prints `prompt_type= prompt_kind=` and `kind_acts=`.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-18: opened.
