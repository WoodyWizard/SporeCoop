---
id: BUG-0600
title: Tribe: a guest's game plays none of the hit and fight sounds of the members' fights
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: lab (fix/w44-tribe-guest, BUG-0568 analysis)
role: guest
versions: 
fixed_in: 0.7.8
branch: fix/w44-guest-player3
commits: 625e1260
tests: tribe_fight_sounds
signature:
docs: docs/re/tribe.md#tribe-mirror-fx
related: BUG-0603, BUG-0568, BUG-0569
---

## Report
Found in the lab while analysing BUG-0568 (no player report yet): in a 40 s fight of the host's members (tribe
fixture, `coop.tribe act 7`), the host's game played the fight's hit / weapon sounds 0xA7E3E1FB (18), 0xD05B58ED (17),
0xD967193C (5), 0xCE5F2ED0 (3) (caller chain 0xA3749B, 0xA83B18: effect-attached sounds) and the creature voices
0xC881120E / 0x529FEC69 (0xC1B0CC / 0xC1B170); the guest's game none of them (`coop.audio list`, out/lab/20261105-115203).
Reproduce: two games on the tribe fixture, the host's members attack an NPC member, then `coop.audio list` on both.

## Root cause
~ superseded (2026-11-05, fix/w44-guest-player3): the first guess below; see the observed cause after it.
Inferred, not analysed: the sounds come with the effects and the hit reactions of the host's members (TakeDamage, the
behaviour trees' weapon effects); a guest's mirrors take no hits of their own and run no tree. ~ The effects relay
(effects.cpp) covers the players' own creatures, not the members. Not fixed (out of the scope of A1, 0.7.8).

**Observed** (fix/w44-guest-player3, lab lanes 4 / 5, `coop.audio` + `coop.effects log on` with the creators' return
addresses, out/lab/20261105-134329, out/lab/20261105-135554; docs/re/tribe.md#tribe-mirror-fx): the host's game made
these sounds from calls of its behaviour trees on the members, which a guest's mirror (no tree) never makes:
* the combat voices 0xC881120E / 0x529FEC69: `SporeCoop::Creature_CombatVoice` 0xC1B030 (the fight node 0xD69540,
  TakeDamage);
* 0xD967193C: the sound of the sprint effect 0xE55DC391 (pool 1, key 0x35052B7), which cCreatureBase::Update starts
  from the sprint bit `SporeCoop::Creature_SetSprint` 0xC1AE70 sets (behaviour nodes);
* the members' other pool effects (0x8ADA6617, the tools' one-shots 0xF8122731 / 0x759B0E13, 0x21C934A7, 0x85A02492):
  made by the game's effect pools, reported by the pool hooks, relayed for the own avatar only (effects.cpp).
Not this cause: 0xCE5F2ED0 (the order marker 0xA231DA2F at the target, the ordering player's own feedback) and the
attack animations' per-target hit impacts 0xD05B58ED / 0xA7E3E1FB (animation events, BUG-0603, not fixed).

## Fix
`mirror.fx` (mods/coop/mirror_fx.cpp, MSG_MIRROR_FX 64): one relay of what the host's game does to a creature a guest
mirrors (a tribe member or an npc.cpp NPC): the pool effect starts / stops / one-shots (effects.cpp passes every
creature but the avatar and the puppets on), CombatVoice and SetSprint (hooked, sent when +0xBB1 / the sprint bit
changes). A guest makes the same game call on its mirror (the game's own pools with the bone check, the original
CombatVoice with force, the original SetSprint, whose Update then starts the sprint effect). `coop.fix mirror.fx off`:
nothing sent or played (0.7.7). `coop.mirrorfx` prints the counters. Creature-stage NPCs use the same path: not tested.

## Verification
coop_tribe_fight_sounds.py (`tribe_fight_sounds`): the effect-component sounds and the voices of a 40 s fight, per
sound id, the guest's game at least 80 % of the host's.
* FAIL with `SPORE_FIX_OFF=mirror.fx`: out/lab/20261105-140001 (4 of 15, voices 0 of 4), out/lab/20261105-135355 (0 of
  10, 0 of 4), out/lab/20261105-135119 (0 of 12, 0 of 4).
* PASS: out/lab/20261105-135946 (2 runs: 9 of 9, voices 4 of 4 each).

## History
- 2026-11-05: opened.
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): analysed at runtime, fixed (mirror.fx); the animation per-target hit impacts split to BUG-0603.
- 2026-11-05: released in 0.7.8.
