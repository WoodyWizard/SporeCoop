---
id: BUG-1242
title: Creature: the second player cannot befriend the other creatures (social game)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w52-social
commits: 21fe1d86 3dfa164f
tests: creature_social_kinds, creature_social_kinds_keys (tools/tests/coop_creature_social_kinds.py)
signature:
docs: docs/re/creature.md#creature-social-kinds, docs/06-testing.md#testing-social-kinds
related: BUG-0179, BUG-0518, BUG-0525
switch: social.kinds
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
Test `creature_social_kinds` (tools/tests/coop_creature_social_kinds.py, fixture creature_cadian): the guest's real
clicks on the bar's social buttons answer each prompt of an animal of a nest that prompts to charm / dance / pose (the
`--keys` twin `creature_social_kinds_keys`: the keys 1-4). Checked in both games: each answer of another kind than sing
plays its animation on the guest's avatar and the host's puppet, the animal is impressed (the NPC's and the mirror's own
flag), the species table counts it.
- FAIL with SPORE_FIX_OFF=social.kinds: out/lab/20261118-105625 (lane 6, both tests: the guest's charm / dance clicks
  and keys play nothing on either game, the games fail after their prompt lists), out/lab/20261118-105625-2 (lane 7,
  the click test; its keys run found no usable creature / nest in 4 starts), earlier versions of the test:
  20261118-103216-2, 20261118-101931, 20261118-101553-2.
- PASS: out/lab/20261118-105411 (lane 7) and out/lab/20261118-105411-2 (lane 6), both tests each (dance, pose, charm
  answers performed on both games, the animal impressed in both); earlier versions 20261118-104620 / -2, 20261118-102942 / -2.
- Regression: creature_social_ally, creature_social_bar, creature_social_meter, social_console out/lab/20261118-103216
  (ok); creature_social_prompts failed there once (its known flake: "the host counts that social game as failed"),
  PASS on the rerun out/lab/20261118-103837.
- Seen with the fix off (out/lab/20261118-100728): a guest whose sing outweighs the animal's prompts still impressed it
  by singing only; the players' creatures (low levels against later nests) could not, as in the video.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-social): root cause found (the social kind +0x10 read as a flag), fixed (social.kinds), FAIL/PASS
  twin creature_social_kinds (+ _keys) on the campaign's world.
