---
id: BUG-0603
title: Tribe: a guest's game plays none of the hit impact effects of the members' attack animations (the animation event's per-target effects)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: lab (fix/w44-guest-player3, BUG-0600 analysis)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w45-tribe
commits: a0b44e09
tests: tribe_hit_impacts (coop_tribe_hit_impacts.py)
signature:
docs: docs/re/tribe.md#tribe-mirror-fx, docs/re/tribe.md#tribe-hit-impacts
related: BUG-0600
---

## Report
Found in the lab while fixing BUG-0600 (no player report yet): in a 40 s fight of the host's members (tribe fixture,
`coop.tribe act 7`) the host's game played sound 0xD05B58ED 14 times (out/lab/20261105-135554, probe with
`coop.effects log on`) and in other runs 0xA7E3E1FB 18 times (out/lab/20261105-135339, out/lab/20261105-115203); the
guest's game none, also with BUG-0600's fix on. Reproduce: coop_tribe_fight_sounds.py, read the `host` / `guest` lines.

## Root cause
Observed (lab + Ghidra): 0xD05B58ED is the sound of effect 0x682E8D4B, an animation event's effect (made through the
animation system's effect wrapper 0x9FBF00 from the event handler 0x9A6D70, kind 1); its siblings 0xF0D5A994 and
0xCFECC623 likewise (host 13-14, guest 0). Other effects of the same animation (0x6397C4AC, 0xE6D770B8) play on both
games: the guest's mirrors play the animation (the BUG-0569 relay). The handler makes the missing ones in its loop over
param_18 entries (FUN_009a47a0 filters them by the event's flags & 0x70 against a list): inferred, the attack's target(s);
a mirror's relayed animation has no target. 0xA7E3E1FB: inferred the same class (same sound path, not matched to an effect).
Not fixed (BUG-0600 relays the pool effects, sprints and voices only).

2026-11-05 (fix/w45-tribe, T2 of 0.7.9): the target-list inference above was wrong (CORRECTION in
docs/re/tribe.md#tribe-hit-impacts).
* Observed (runtime, lab 20261105-230956): AnimEffect_SetTarget 0x9FBC10, the handler's per-target call (param_18 > 0),
  was not called once in a 40 s fight with 58 impact sounds on the host. The impacts are made through the
  param_18 == 0 path, return 0x9A72E3.
* Observed (Ghidra): the handler's only caller 0x9A7720 resolves the event's object spec (0x9A5560) to a list of objects
  and calls the handler once per object, with param 2 the animating instance and param 3 the resolved object's
  instance; with no object, no call.
* Observed (runtime, lab 20261105-231448, hook on the handler): on the host 127 of 477 event effects in the fight were
  made for another object than the animating one (param 3 != param 2): 0x682E8D4B 25, 0x594BDFF9 24, 0x7B5A2E62 24,
  0xF0D5A994 18, 0xCFECC623 18 and 4 more ids, at the victim's hit spot. On the guest 0 of 454 (all on the animating
  mirror itself). So the hit impacts' spec resolves to the attack's target, which the game gives the attacker's
  animation; a mirror's relayed animation has no target (inferred: how the game sets that target was not followed).

## Fix
`effects.hit_targets` (mods/coop/game.cpp hook of SporeCoop::AnimEvent_Handle 0x9A6D70, mods/coop/effects.cpp): an
animation event's effect made for another object than the animating one is sent as a world effect (MSG_EFFECT pool
5, the effect id at the position the game gave it) to the players in the same place, and played there through the
existing world-effect path (its own rate limit, the 8 s live list). Every game sends what its own game made, so it
also covers the host's own creature and NPCs seen by a guest in Creature and a guest's creature seen by the host
(not tested there). `coop.fix effects.hit_targets off`: nothing sent (0.7.8). `coop.effects` prints `effects hits:`.

## Verification
coop_tribe_hit_impacts.py (`tribe_hit_impacts`): an NPC tribe's member put 9 m from the player tribe's, attacked for
40 s; the impact sounds (0xD05B58ED, 0xA7E3E1FB, 0xF0D5A994, 0xCFECC623 through an effect's sound component) the
guest's game played, per sound id, at least 80 % of the host's (`coop.audio`, the game's own audio commands).
* FAIL with `SPORE_FIX_OFF=effects.hit_targets`: out/lab/20261105-231635 (0 of 53).
* PASS: out/lab/20261105-231808 (52 of 56), out/lab/20261105-231448 (49 of 51, with HIT_LOG=1).
* Without the victim put next to the attackers some runs had no fight in 40 s (0-3 impacts on the host:
  out/lab/20261105-225737, -225921, -230055, -230304): the setup is why the test puts it there.

## History
- 2026-11-05: opened (fix/w44-guest-player3, P2b of 0.7.8), analysed, not fixed.
- 2026-11-05 (fix/w45-tribe, T2 of 0.7.9): root cause corrected (the event's object, not a target list); fixed behind effects.hit_targets.
