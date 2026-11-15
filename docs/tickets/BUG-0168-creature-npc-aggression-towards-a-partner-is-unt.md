---
id: BUG-0168
title: Creature: NPC aggression towards a partner is untested end to end
status: fixed
kind: test
stage: Creature
reported: 2026-10-25
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in: 0.7.11
branch: fix/w46-g4-tests
commits: acda7997
tests: creature_aggression (tools/tests/coop_creature_aggression.py)
signature:
docs: docs/re/creature.md#creature-npc-aggression
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Since 2026-10-25 an NPC on the host that is nearer to a partner's creature ticks its AI with that
  creature as the player (docs/re/creature.md 14.2): it notices the partner (checked: the "noticed"
  stimulus names the puppet, `coop_creature_kills.py`), and retaliation and epics hurt the partner
  (they did before too). That an NPC of an aggressive archetype then attacks the partner on sight
  is not checked: no such species in the `creature` fixture attacked even the host's avatar on
  sight, and spawned epics (`coop.npc epic`) do not attack the avatar.
* To check it: a save with an aggressive (red face) nest nearby, the guest next to it and the host
  60 m away: `damage_in` on the guest should grow. Side effects to watch: avatar-only UI of the
  host (hints, music) while an NPC hunts the partner.

## Root cause
The missing piece was a hostile species: no species of the `creature` fixture is hostile at the start. Observed (Ghidra
static, docs/re/creature.md#creature-npc-aggression): the behaviour tree's on-sight attack (node 0x04A1A1E6) needs the
species relation 2 or 5; a player gets 5 by eating an egg of that species' nest (the egg node 0xD716F0 calls
ApplyEvent(7, species, null)), 2 by walking up to one, 5 by killing a young one. Observed at runtime (this branch's
runs): after that event an adult NPC of the species bites the guest standing next to it 45-50 m from the host, in the
guest's own game (its health falls); before it the same NPC leaves the guest alone. Also when the guest plays its own
creature (another species than the host's: the static worry that the 0xD7DA48 PlayerSpecies compare would skip a
partner of another species did not show) and with `coop.npc partners off` (the attack does not need the mod's AI swap).
No mod bug found.

## Fix
The missing test, tools/tests/coop_creature_aggression.py (suite creature_aggression, creature fixture): the host's game
eats an egg of a species the way the avatar's egg node does (`coop.social event <species> 7`, a new host test command:
the game's own ApplyEvent, social.cpp debug_event), the guest stands next to an adult of that species far from the
host with no input: its health falls in its own game, the host's NPC has the puppet as its target; a control phase
before the event. Twins: AGGR_EVENT=none (no event) must FAIL; AGGR_OWN=1 (the guest's own creature) and
AGGR_PARTNERS=off pass.

## Verification
FAIL (the twin, AGGR_EVENT=none: relation stays, the guest is not hurt): out/lab/20261108-054135.
PASS 7 of 7 on lane 3: out/lab/20261108-052423, 20261108-053723-2 (#1-#4), and the variants AGGR_OWN=1
20261108-052925, AGGR_PARTNERS=off 20261108-052641.

## History
- 2026-10-25: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached. Needs a save with an aggressive (red face) nest near the avatar; the creature fixture has none and making one means playing to it. Left open (a fixture task, not a test-only change).
- 2026-11-08 (fix/w46-g4-tests): the end-to-end test written (the game's egg event makes a species hostile); 7 of 7 PASS, the twin FAILS; fixed.
- 2026-11-08: released in 0.7.11.
