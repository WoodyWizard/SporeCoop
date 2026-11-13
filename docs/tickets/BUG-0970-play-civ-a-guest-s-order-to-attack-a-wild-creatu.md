---
id: BUG-0970
title: play_civ: a guest's order to attack a wild creature is rejected by the host (the creature is not within 10 m there)
status: fixed
kind: desync
stage: Civ
reported: 2026-11-12
source: rel/0.7.13 integration out/lab/20261112-155526 play_civ
role: guest
versions: rel/0.7.13
fixed_in: 0.7.13
branch: fix/w48-civcmd
commits: bc0bd209
tests: civ_guest_creature, play_civ
signature: play_civ "guest commands: sent N, the host executed N-k and rejected k"; host log "rejected command kind 0 ... target kind 3 id 0 noun 018EB45E at (x y z) not in the host's game"
docs: docs/re/civ.md#civ-wild-creature-orders
related: BUG-0325, BUG-0940
---

## Report
rel/0.7.13 integration run out/lab/20261112-155526, play_civ (default seed, 10 min): "PROBLEM guest commands: sent
108, the host executed 105 and rejected 3", the run's only problem. The host's periodic `civ(host)` status shows
rejected=0 up to s 530 and rejected=3 from s 570; the host's game log has the three at s 546.7 (16:04:59.705), one
frame: `civ: rejected command kind 0 from player 1 (vehicle 11|17|14: target kind 3 id 0 noun 018EB45E at (-391 320
-68) not in the host's game)`. Kind 0 is an order, target kind 3 an object by noun + position, noun 0x18EB45E a
wild creature (cCreatureAnimal). Three vehicles in the same frame: one group order of the guest's game (a right-click
on an object orders every selected vehicle, docs/re/civ.md 4), i.e. the guest's bot right-clicked a move destination
(a_move) or a defend point (a_defend) and a wild creature was under the cursor in the guest's game.

## Root cause
Observed (logs above, lab 20261112-164639 and 20261112-165013): resolve_target looks for a target of kind 3 within
10 m of the guest's position in the host's game; the host had no wild creature there, so the orders were rejected
and the vehicles did nothing (on both games: the guest's own SetOrder is swallowed and sent to the host).
Observed (lab lane 4, probe over 5 min on civ_npc, no play): right after the load both games have the same wild
creatures within 0.0-3.8 m of each other (a herd of 10 standing in one place); wild creatures are not synced in Civ
at all (civ.cpp has no creature code), so once they move apart (a chase, a flight, a death by the host's vehicles,
which on the guest are mirrors that do no damage) a guest's attack of one is lost. Inferred for the run: the herd
near (-391 320 -68) had moved or died on the host by s 546 (not logged).
Not a test artefact of the BUG-0940 kind: a player's right-click on a wild creature with the guest's vehicles is lost
the same way. Also observed: every game has a wild creature object without a species at (0 0 0) (not placed in the
world, the census skips it), so a target search by distance alone may find it from far away (only the tests'
`coop.civ attack <id> c` did that; the fix's search is within 40 m of the clicked place).

## Fix
civ.cpp host_command (CMD_ORDER): a wild creature target the host does not have within 10 m becomes the host's
wild creature nearest to that place within 40 m (attacked instead), else a move order to the place (the order a
right-click on the ground gives), executed, not rejected; `creature_orders=<near>/<moves>` in the host's civ status;
a resolved object target is logged (first 20). Switch `coop.fix civ.creature_target off` (or
SPORE_FIX_OFF=civ.creature_target): the old rejection. Tests: `coop.civ creatureshift <x y z> <r> <d>` (moves this
game's wild creatures around a place d m aside), `coop.civ attack <id> c<x>,<y>,<z>` (the guest game's attack order
on its creature there); `coop.census creatures` now prints each row's noun (tells a citizen from a wild creature).

## Verification
Test civ_guest_creature (tools/tests/coop_civ_guest_creature.py, fixture civ): the host moves its creature 80 m
aside, the guest's game orders its two vehicles to attack its own creature there.
FAIL with the switch off: out/lab/20261112-165013 (host rejected 2, orders -1, vehicles 0 m closer on both games;
the same host log line as the integration run); also 20261112-164639.
PASS with the fix: out/lab/20261112-164915 and 20261112-164815 (orders 6 on the host, 28-30 m closer on both games,
nothing rejected).
play_civ with the fix, lane 5: out/lab/20261112-165156 (guest commands 81 sent, 81 executed, 0 rejected) and
out/lab/20261112-170402 (85 / 85 / 0), both PASS. make test PASS.

## History
- 2026-11-12: opened.
- 2026-11-12 (fix/w48-civcmd): traced to an unsynced wild creature target; fixed (civ.creature_target), test civ_guest_creature.
- 2026-11-12: released in 0.7.13.
- 2026-11-13 (fix/w49-civ): with BUG-0973 the host owns the wild creatures; civ_guest_creature (this ticket's case: games
  whose herds differ) now runs with `npc.civ_wild` off.
