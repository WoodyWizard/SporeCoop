---
id: BUG-0744
title: Tribe: a guest's gather order on a fruit (cFruit 0x2C9CC91) the host's game no longer has within 8 m is rejected (a fruit on the guest only, after BUG-0472/0497)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-08
source: G4 analysis: play_tribe w46-k4-bodies out/lab/20261108-020506-2
role: guest
versions: 
fixed_in: unreleased
branch: fix/w46-b1-tribe
commits: 486ed7d1
tests: tribe_fruit_order, play_tribe
signature:
docs: docs/re/tribe.md#tribe-fruit-order
related: BUG-0472, BUG-0497, BUG-0531
switch: tribe.fruit_order
---

## Report
play_tribe in fix/w46-k4-bodies out/lab/20261108-020506-2 failed "the host rejected no guest order (1: ... action=2
target=2/0/2C9CC91 ... rejected: no such target here)": the guest's bot right-clicked a fruit (noun 0x2C9CC91, cFruit) in
its game; the host's game had no such fruit within 8 m of that point. Seen once in 62 play_tribe runs of 2026-11-06..08.
Reproduce: play_tribe --repeat N; the guest's log names the order, the host's the reject.

## Root cause
Observed (runtime, lab 20261108-144053, lane 7, `coop.fix tribe.fruit_order off`): the guest's Gather order (its
DoActionForObjects with CitizenAction 2 on a cFruit 0x2C9CC91 of its own game) on a fruit the host's game no longer had
within 8 m reached the host as `target=2/0/2C9CC91 (412.2 52.6 292.7) -> 00000000 rejected: no such target here`: the
guest's member got no order on the host (rec=-1). The precondition is made by the test (`coop.tribe unfruit` on the
host): in play each game's loose fruit roll, are eaten and are recycled their own way (BUG-0472 / BUG-0497), so a
fruit the guest sees can be gone on the host for a moment until the wobj sync removes it (inferred for the play_tribe
case of 20261108-020506-2).

## Fix
mods/coop/tribe.cpp host_command: a gather order (CitizenAction 2) on a fruit (target kind 2, noun 0x2C9CC91) that
resolves to nothing on the host takes `fruit_instead`: the nearest planet tree with fruit left (cTreeData +0x48 != 0)
within 12 m, else the host's nearest fruit within 15 m. The order then is a gather on both games (the guest's member
walks to the host's tree). `tribe last order` says "(no such fruit here: tree|fruit <ptr> instead)". Test commands
`coop.tribe fruit <x y z>` (the selected members' gather on this game's fruit there, as a right-click gives) and
`coop.tribe unfruit <x y z> <r>` (host: its fruit there destroyed). Switch `coop.fix tribe.fruit_order off` (rejected,
0.7.11).

## Verification
Test tribe_fruit_order (coop_tribe_fruit_order.py, new, cold, ~1 min): the guest selects its member, the host's game
loses every fruit within 8.5 m of a fruit both games have, the guest orders the gather on its fruit there; the host
must take it (no reject), its member must have a Gather order with a target in the host's game, and the guest must see
the member walk 2 m nearer.
* FAIL (SPORE_FIX_OFF=tribe.fruit_order): out/lab/20261108-144053-fix-w46-b1-tribe (lane 7): "rejected: no such
  target here", the member's order record rec=-1. (That run's last check was a plain 2 m move, which passed while the
  member wandered; it is now "2 m nearer the fruit".)
* PASS: out/lab/20261108-144200-fix-w46-b1-tribe #1, #2 (lane 7): "no such fruit here: tree ... instead", rec=2 with the
  tree, the guest's mirror 10.6 / 9.4 m nearer in 12 s.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-b1-tribe): reproduced with a new test (FAIL with the switch off), fixed with tribe.fruit_order, 2 PASS.
