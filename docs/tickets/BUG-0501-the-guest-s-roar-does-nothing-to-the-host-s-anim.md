---
id: BUG-0501
title: the guest's roar does nothing to the host's animals (split from BUG-0462)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: split from BUG-0462 (player report 2026-11-02)
role: guest
versions: 
fixed_in: 0.7.8
branch: fix/w44-guest-player2
commits: 891eeac1
tests: creature_roar (coop_creature_roar.py)
signature:
docs: docs/re/creature.md#creature-roar, docs/re/creature.md#creature-roar-2, docs/re/creature.md#creature-roar-relay
related: BUG-0462, BUG-0418
switch: creature.roar_relay
---

## Report
The second half of BUG-0462's host mark: "ROAR ability is broken for guests and work only host". Not analysed on
fix/w40-creature-acts (2026-11-03): which ability the players call the roar is not known (neither fixture creature has
an ability named so; the types are 0xD 0xE 0x10 0x11 0x1D 0x1E 0x25 0x28 0x31 0x32 0x33). A guest's ability plays on its
own avatar and its puppet (animation relay), but its effect on NPCs (a scare / social stimulus posted by the ability
code with the avatar as sender) runs only in the guest's game, where the NPCs are AI-less proxies; the host's NPCs never
get it (class B, docs/sync-architecture-plan.md L1: a request to the owner). Inferred, not observed.
To do: find the ability (Ghidra: the callers of BT_PostStimulus 0xBC9940 in the ability code), relay its stimulus to the
host as a request, the host posts it with the guest's puppet as sender.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not fixed (fix/w40-npc-anims, 2026-11-03, about 20 minutes of static analysis). Observed (Ghidra): the roar is the BT
blackboard variable `HeardRagingRoar` (global 0x169F134, end callback 0xD85400 shared with HeardSirenSong); its
setter, the ability that roars, was not found (the global is referenced only by its static init). Inferred: the
roar affects the NPCs of the roaring player's own game only. docs/re/creature.md#creature-roar. A test needs a
creature with the Raging Roar (a carnivore Cell's consequence ability); the fixtures have none.

* Observed (Ghidra, fix/w44-guest-player2): the roar is the BT action node 0x06005DAF, tick 0xD8E840
  (`SporeCoop::BT_SpecialAbilityTick`): on its first tick it gives every creature within the ability's range that is
  not the avatar, alive, of another species and not epic the stimulus (0, 0x4000000, FLT_MAX, roarer) and the goal
  slot (template 1, group 1) for the ability's 25 s (docs/re/creature.md#creature-roar-relay).
* Observed (runtime, lane 6): the guest's roar reached only its proxies; the host's 4 NPCs within 20 m of the
  guest's puppet got no slot and did not move (out/lab/20261105-110044). The roar is the player's consequence ability:
  the guest's avatar has it (index 6 of 9), its puppet in the host's game does not (6 abilities), so the host could
  not simply replay the ability on the puppet.

## Fix
`player_rules.cpp` (`creature.roar_relay`): a hook on the node's tick 0xD8E840. A guest's first tick of its own roar
sends MSG_PLAYER_RULE kind 2 {type 0x31, range, seconds} to the host; the host's game makes the node's four calls
(BT_PostStimulus, BT_GoalSlotStart, BT_ClearStimulus, BT_GoalSlotEnd of the siren's slot) on its creatures within the
range of the guest's puppet with the node's filter, the puppet as the roarer. The NPCs' flight reaches the guest as
their states. `coop.fix creature.roar_relay off`: the roar stays in the guest's game. Not covered: the siren song
(0x32), the guest's own Summon Flock (0x33).

## Verification
coop_creature_roar.py (suite creature_roar): the guest stands 6 m from a group of the host's wild NPCs and roars
(`coop.rules roar 31`: the node's first tick through the hook, as the ability starts it); in the host's game every NPC
of another species within 20 m of the puppet must hold the roar's goal slot (the game's slot table) and half of them
must run the game's answer node 0x02D852E0 within 3 s, and their mean distance from the puppet must grow by 1.5 m or
more in 10 s.
* FAIL with `SPORE_FIX_OFF=creature.roar_relay`: out/lab/20261105-115528 (0 of 4 slots, 0 of 4 in the node, +0.0 m),
  out/lab/20261105-110044 (an earlier version of the test).
* PASS: out/lab/20261105-115630 (cold), out/lab/20261105-115727 (warm, after creature_nest_ally); earlier versions
  out/lab/20261105-110141, out/lab/20261105-105935. In warm games the NPCs back away 2-4 m (8-17 m cold).

## History
- 2026-11-03: opened.
- 2026-11-03: likely reported again (0.7.3.6, guest's report 20261104-012758, text 'ability doesn't work', Creature; the mark itself has no text). Not confirmed which ability.
- 2026-11-03: analysed on fix/w40-npc-anims (static only), not fixed.
- 2026-11-03: second look on fix/w41-creature-left (Ghidra, ~20 min): the 0x400000 posters checked (0xC23A10 is the perception scan, not the roar), the ability kinds of 0xC1E8B0 written down; not fixed: a test needs a creature with the Raging Roar (no fixture has one). Next: a fixture from a carnivore Cell, then the host replays the guest's area ability (kind 2 / 3) from its puppet.
- 2026-11-04: third look on fix/w42-creature-left (runtime, `coop.social abil`): no fixture creature has an ability of the area kinds 2 / 3 (all kind 0, one kind 1 type 0x1D); 0xC155D0 is the ability's effect spawner, not a stimulus. Still no test creature; not fixed. docs/re/creature.md#creature-leftovers-3.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not fixed. New (Ghidra, from the BUG-0556 analysis): the ability handler 0xD8E840 runs kind 0x31 (roar: stimuli to the enemies around the roaring creature), 0x32 (siren song), 0x33 (summon flock -> drama kind 8). A guest's roar posts its stimuli to the AI-less proxies in its own game only; a fix is a guest -> host MSG_PLAYER_RULE kind that runs the same kind on the puppet in the host's game.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): fixed (creature.roar_relay), test creature_roar.
- 2026-11-05: released in 0.7.8.
