---
id: BUG-1032
title: Space: Rivals at war: the host's escorts and Defenders and a guest at war with the host cannot damage each other (hits dropped as friendly)
status: fixed
kind: bug
stage: Space
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-3
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-space
commits: f95e96fe
tests: space_war_escort
signature:
docs: docs/re/space.md#space-war-npc-hits, docs/known-issues.md#issue-war-escort-hits
related: BUG-0192, BUG-0420, BUG-0705
---

## Report
Code audit for 0.7.14, finding SPC-3. Not seen at runtime, found by code reading.
In Rivals with a guest at War with the host, the host's escort ships and the Defenders of its systems attack the guest's puppet (its political id is -1, an enemy to the game), but the guest's real ship never takes the damage; the guest's shots at the host's escorts (their mirrors on the guest) do nothing on the host either. The player sees the host's escorts shooting at it without effect and cannot destroy them.

## Root cause
Observed in code (main efd2e0ba), confidence high:
* space_rivalry.cpp:640-656 puppets_tick gives the puppet of a player at war with us political id kEnemyPol (-1): the host's own NPC ships of its empire now target it.
* squadron.cpp:279 (host h_TakeDamage): `own = ... && friendly(att)`; friendly() (squadron.cpp:153-159) is true for any attacker with the host's empire id, so the escort's hit on a War puppet is dropped and no MSG_UFO_DAMAGE goes to the guest. BUG-0192 documents the opposite: NPC ships of the host hit an enemy puppet and the hit is relayed to its owner.
* squadron.cpp:1107 (host, MSG_UFO_HIT): a guest's shot at an escort of the host's empire is dropped by friendly() without asking rivals()/relation; and :1116 applies the hit with the host's own empire id (PlayerEmpireID) as the attacker's political id, so even without the filter the game skips damage within one empire (BUG-0705's runtime finding).
The BUG-0420 filter (space.friendly_hits) was written for Allies, where the game ignores such hits anyway; in Rivals at War it removes real hits. The audit's word "asymmetric" is only half right: both directions are dropped; the visible asymmetry is that the host's escorts fire at the guest at all.

## Fix idea
friendly(att) and the MSG_UFO_HIT filter: not friendly when spaceext::rivals() and the hit's other side is a player at War with the empire's owner (the host); MSG_UFO_HIT from a War guest applies the hit with kEnemyPol as the attacker's political id (or the guest puppet's own). Owner decision first: should the host's escorts and Defenders fight a guest at war (BUG-0192 says NPC ships of the host do)? Switch: `coop.fix space.war_npc_hits off`.

## How to test
New coop_space_war_escort.py (Space fixture, Rivals): host and guest at War, the host spawns an escort of its empire (`coop.fleet spawn own`), the escort is made to hit the guest's puppet (`coop.fleet hit` with the escort as the attacker) and the guest shoots the escort mirror; assert the guest's ship hp (its game) and the escort's hp on the host go down. Main: FAIL (friendly_dropped grows, hp unchanged).

## Fix
squadron.cpp, switch `coop.fix space.war_npc_hits` (off: 0.7.13). Owner decision 1 (2026-11-13): in War the host's
escort and Defenders fight the guest, hits count both ways.
* Host, h_TakeDamage: an escort's (the host's empire's) hit on the puppet of a player at War with the host is not
  friendly: it goes to that player as MSG_UFO_DAMAGE (`war_hits_out=`).
* Host, MSG_UFO_HIT from a War guest: not dropped by the BUG-0420 filter; a hit on a ship of the host's empire is
  applied with no empire (-1) as the attacker's political id (the game drops a hit whose attacker political id is the
  victim's); any other with the host's empire id as before (`war_hits_in=`).
* Guest: at War with the host, the mirrors of the host's empire's ships get no empire (-1) in this game, like the War
  puppets on the host: in a co-op session the guest's game plays the host's save and has the same empire id
  (800021CA on both in the lab), so the game took them for its own ships (no target, no damage); MSG_UFO_DAMAGE from
  such a ship is applied with -1 (`war_damage_in=`).
* rivalry::at_war(id) (space_rivalry.cpp): rivals mode on and this game at War with player id.
Allies, peace and neutral: unchanged (BUG-0420's filter).

## Verification
`coop_space_war_escort.py` (suite `space_war_escort`, fixture space, ~1 min): Rivals, the guest declares war on the
host, the host spawns an escort of its empire and a ship without one; the escort hits the guest's puppet 3 x 20 (the
guest's ship hp in its game), the guest shoots the escort's mirror 2 x 10 (the escort's hp in the host's game), a
control shot on the empire-less ship, then peace and Allies (the escort's hit is friendly again).
* FAIL (SPORE_FIX_OFF=space.war_npc_hits): out/lab/20261113-013047 (guest 1500 -> 1500, escort 50 -> 50)
* PASS: out/lab/20261113-013314-2, out/lab/20261113-015436-2 (guest 1500 -> 1440, escort 50 -> 40, control 50 -> 48, Allies: no damage)
Observed at runtime: the root cause as in the code (friendly_dropped grew, no damage), plus the guest's mirrors of
the host's escorts carrying the guest's own empire id. Real weapon fire between the guest's ship and the escort
was not driven by a bot (the test uses the hook path the projectile takes).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-space: War hits count both ways (owner decision 1); FAIL/PASS in the lab.
- 2026-11-14: released in 0.7.14.
