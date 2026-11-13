---
id: BUG-0896
title: Creature: a guest's click at an NPC egg that had just hatched on the host killed the hatchling (a body at the egg's place, the host credited a hunt): the hatch reached the guest up to a second late
status: fixed
kind: bug
stage: Creature
reported: 2026-11-10
source: owner, watching a player's recording 2026-11-10 (two screenshots)
role: guest
versions: 
fixed_in: 0.7.13
branch: fix/w48-reports
commits: 85e77d3d
tests: nest_egg_hunt
signature:
docs: docs/06-testing.md#testing-nest-egg-hunt
related: BUG-0660, BUG-0384, BUG-0710
---

## Report
The owner watched a recording of a co-op session (version unknown, the screen is probably the host's). An NPC
nest's egg (glowing under the mouse) stood next to the players; the guest did something with it (ate it or hit it,
not clear from the recording). A moment later a small creature's body (a hatchling by its size, on its back) lay at
the egg's place and the mod's notice said "+4 DNA from Player's hunt" (the guest still had the default name).
Two screenshots only; no report file yet.

## Root cause
Not known. Two readings:
* No bug: the egg hatched at that moment and the guest killed the hatchling (half health, one bite); the body and
  the hunt DNA are then right and the place is a coincidence.
* A bug (inferred, low confidence): by the notes (docs/re/creature.md#creature-nest-eggs) a guest's eat of a
  mirrored egg sends MSG_NEST kind 1 and the host only calls cEgg::SetState(4) on its egg: no creature, no hunt
  DNA. Candidates: (a) a race at the hatch: the guest still shows the egg mirror (the host's egg state arrives at
  1 Hz) while the host's egg already hatched (state 3) and its hatchling exists; the guest's action lands on the
  hatchling's proxy or the host maps it to the hatchling; (b) the guest's click on the egg is sent as an attack
  (MSG_DAMAGE / the bite path) on the nearest NPC proxy; (c) SetState(4) on an egg that is hatching (state 3 or the
  hatch countdown +0x1F0 at 0) leaves a hatchling that dies at once (a body with no fight).

## Fix idea
Depends on the cause. For (a) and (c): the host refuses a guest's eat of an egg that is hatching or hatched and
answers with the egg's real state; the guest removes its mirror at once on that answer. For (b): the guest's eat
node on an egg mirror must not also send a hit.

## How to test
Two games in Creature at an NPC nest with eggs (the nest_eggs tests' fixture): (1) the guest eats an egg mirror:
on the host the egg is state 4, no new creature and no body within 5 m, the DNA total unchanged, no hunt notice;
(2) the same at the hatch moment (set the host egg's +0x1F0 countdown to 1 s, the guest eats within that second);
(3) the guest charges into the egg (the hard-collision break). Check the host's noun list for a creature made and
killed in that second.

## Root cause (2026-11-12, fix/w48-reports)
Traced in the code, the window measured in the lab (the recording itself was not available):
* A guest's eat of an egg mirror (MSG_NEST kind 1) only makes the host's egg state 4 (nest.cpp on_eat): no creature,
  no DNA. A hit on an egg sends nothing (damage_filter only takes creatures in g_proxy_of). So the egg never paid the
  "+4 DNA from Player's hunt": that notice comes only from a lethal MSG_DAMAGE_REQ of the guest on a host NPC
  (npc.cpp, dna_ledger g_credit 2.5 s). The guest killed a creature.
* The host sent its eggs once a second (unreliable snapshot). When the host's egg hatched (state 3, the hatchling made
  at the egg's place, half health), the hatchling reached the guest as an NPC proxy within ~0.1 s, while the guest's
  egg mirror stood whole until the next snapshot. Lab: with the hatch right after a snapshot the guest showed the
  hatchling inside a whole egg for 0.91 s (FAIL run). A click or bite at the glowing egg in that time lands on the
  hatchling: one hit kills it, a body on its back at the egg's place, the host credits the guest's hunt. That is the
  recording (inferred for it, observed for the mechanism).
* The name "Player" in that notice: BUG-0897 (the guest's name was the default).

## Fix
`nest.hatch_now` (nest.cpp host_tick): the host keeps the eggs of its last snapshot (game::LiveRef: the game frees
an eaten egg) and checks them every frame; when one hatched, was eaten or is gone, a snapshot goes at once (at most
every 50 ms). A guest's eat of an egg that is not whole on the host is answered with a snapshot at once (logged as
refused). Off: the 1 Hz snapshot only (0.7.12).

## Verification
`coop_nest_egg_hunt.py` (suite `nest_egg_hunt`), lane 5, creature fixture: step 2 (the guest eats a mirror: on the
host the egg is eaten, no creature, no body, no DNA, no hunt), step 4 (the host's egg hatches 0.1 s after one of its
1 Hz snapshots; the guest polled every 0.1 s: a proxy within 1.5 m of a whole mirror for at most 0.4 s).
* FAIL with `SPORE_FIX_OFF=nest.hatch_now`: lab 20261112-052254 (0.91 s). An earlier off run with the hatch at a
  random moment passed (0.12 s, lab 20261112-051854): the window depends on the phase, hence the timed hatch.
* PASS: lab 20261112-051551, 20261112-052559, 20261112-052927 (0.00 s; `early=2` snapshots on the host).
* Not tried: step 3 (an eat in the hatch's second) ran in no run: the herd laid 2 eggs (the test then runs steps 4
  and 2).

## History
- 2026-11-10: opened from the owner's observation; need the recording's time code or a report archive.
- 2026-11-12: fixed (fix/w48-reports): nest.hatch_now; the window measured 0.91 s without it.
- 2026-11-12: released in 0.7.13.
