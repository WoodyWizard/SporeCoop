---
id: BUG-1253
title: Creature: the guest eats a carcass and its puppet in the host's game stands idle
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: host
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-puppet
commits: 7d01ccd6
tests: puppet_eat_anim (tools/tests/coop_puppet_eat_anim.py)
signature:
docs: docs/re/creature.md#creature-puppet-hunger, docs/06-testing.md#testing-puppet-after-social
related:
---

## Report
A players' video: the guest eats a carcass; in the host's game its puppet stands next to it idle. Suggested cause: the eat behaviour node plays its animation through CreaturePlayAnim (0xC122F0), not PlayAnimation (game.cpp); set_behaviour_anim_listener is wired only to NPCs (npc.cpp), and the players' avatars (puppets.cpp) relay only PlayAnimation.

## Root cause
Observed (Ghidra and lab 20261118-122628): the avatar's eat after the player's click on a body runs in its behaviour
tree, whose eat node plays the eat animation (04F65995 for the fixture's creature) with `SporeCoop::CreaturePlayAnim`
0xC122F0. That function does not go through `cCreatureBase::PlayAnimation` 0xC12470 (neither calls the other), and the
players' animation relay (puppets.cpp `on_anim`, MSG_ANIM) hooked only PlayAnimation / PlayAnimationTo; the
CreaturePlayAnim relay of BUG-0464 (`set_behaviour_anim_listener`) is wired for the host's NPCs only. So the guest's
eat (and every other behaviour animation of a player's creature) never reached the partner: on the host's screen the
guest's creature stood idle (02481DE5) next to the body (key frame of the FAIL run).

## Fix
game.cpp reports the local avatar's own CreaturePlayAnim plays outside its idle refresh (not those the mod plays) to a
new listener; puppets.cpp sends them as MSG_ANIM with flag 4 (CreaturePlayAnim; flag 8 its `a`, block its `b`) and the
receiver plays the same call on the puppet (`game::play_behaviour_anim`). An older receiver plays it as PlayAnimation.
Switch `coop.fix creature.avatar_bhv_anims off`: not sent (0.7.15). `coop.status`: `remote animations: ... (behaviour
N); own behaviour sent N`.

## Verification
Test `puppet_eat_anim` (creature fixture): the host kills an NPC by the players, the guest clicks its body (the game's
click action) and eats; the animations its own creature plays while eating must be played by the host's puppet of it.
- FAIL with SPORE_FIX_OFF=creature.avatar_bhv_anims: out/lab/20261118-122628 (lane 5): the guest's own 04F65995, the
  host's puppet none of it (key frame: the puppet stands upright at the body while the guest bends over it).
- PASS: out/lab/20261118-123633 (lane 5): 04F65995 on both (key frame: the puppet bends over the body); again
  out/lab/20261118-124624 and -125657.
- Regression (out/lab/20261118-124624): npc_anims, creature_effects, effects_live, puppet_walk, creature_mating,
  partner_pass, creature_social_prompts OK; creature_eat failed once on its own check race (BUG-1259), OK on the rerun
  out/lab/20261118-125657.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-puppet): reproduced, fixed (creature.avatar_bhv_anims).
