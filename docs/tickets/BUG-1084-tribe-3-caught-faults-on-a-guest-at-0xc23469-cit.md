---
id: BUG-1084
title: Tribe: 3 caught faults on a guest at 0xC23469 (Citizen_HoldBundle, null bundle) while the host is in the Tribe end's City Hall editor
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-13
source: lab
role: guest
versions: rel/0.7.14
fixed_in: unreleased
branch: fix/w49-bundle
commits:
tests: tribe_away_bundle, campaign_tribe_end (lab fault check)
signature: 0xC0000005 at 0x00C23469 SporeCoop::Citizen_HoldBundle+0x9 (data 0x34, edi 0) < Citizen_SetHandheldItem+0xF8D < BT_HealTick+0x30FD
docs: docs/re/tribe.md#tribe-away-tree-end, docs/06-testing.md#testing-w49-bundle
related: BUG-0684, BUG-1076
---

## Report
fix/w49-camp, campaign_tribe_end lab 20261113-142311 (lane 5): the Tribe end the players' way reached Civ, the lab failed
the run on Dave's (extra2, a guest) caught faults: 3 at 14:48:53, while the host was in the Tribe end's City Hall /
vehicle editors (Dave's trace: host_away=1, Tribe). `exception 0xC0000005 at 0x00C23469 SporeCoop::Citizen_HoldBundle+0x9
(data 0x00000034) ... edi=00000000`, called from Citizen_SetHandheldItem+0xF8D < +0xFF2 < ? < BT_HealTick+0x30FD:
BUG-0684's signature (fixed for Civ: a gather node's null bundle), now in Tribe. The game survived (caught).
Reproduce: `lab.py run campaign_tribe_end`; the faults come in the guest's Tribe while the host edits.

## Root cause
* Observed (Ghidra, GOG 2024): the faulting call comes from 0xC2602D in 0xC25BB0 (`SporeCoop::Citizen_BundleStep`,
  new symbol; crash lines said `Citizen_SetHandheldItem+0xF8D`): with its bundle null or of amount 0 or less it plays
  the pick-up animations and then calls Citizen_HoldBundle 0xC23460 with the first bundle of the member's own
  container, null when empty (the fault at 0xC23469). 0xC25BB0 is called by the tick 0xDBC730
  (`SporeCoop::BT_BundleNodeTick`, the campaign's `BT_HealTick+0x30FD`) of node 0x04623E0C in state 3, with the bundle
  the node holds (memory +0x14). BUG-0684's path was another node (the gather node 0xD74D40).
* Observed (runtime, lab lanes 6 / 7): 0x04623E0C is the node that takes up a ground bundle (CitizenAction 14) or
  carries food in hand to the food mat (states 10-12); the gift is node 0x0462855E (the RE table called 0x04623E0C
  "gift food": CORRECTION in docs/re/tribe.md#tribe-away-tree-end).
* Observed (code): a guest's mirrors run their trees only while the host is away (tribe.away, BUG-0151); when the host
  is back they are skipped mid-node, and the world-object sync then removes the guest's own bundles that have no host
  partner (ground after kOwnGrace snapshots; carried: wobj.bundle_carried). BUG-0684's fix (`wobj.bundle_civ_carried`)
  applies only outside Tribe, so it does not cover this path.
* Observed (runtime, tribe_away_bundle with the fix off, lab 20261113-182629, -182823): a member held in 0x04623E0C
  state 3 when the host comes back still holds its bundle 10 s later while the guest's game lists no bundle any more
  (the sync removed it); at the next away the node goes on with the freed bundle.
* Inferred: in the campaign (lab 20261113-142311, Dave) a guest member was in 0x04623E0C when the host came back from
  the Tribe end's City Hall editor (s 1438); its bundle was removed by the sync; 12 s into the host's next away (the
  Civ editors, s 1506) state 3 read the freed bundle's amount as 0 or less and faulted 3 times. The lab runs with the
  fix off reached the stale bundle every time but no fault (the freed block read another amount): the fault depends on
  the heap.

## Fix
`tribe.away_tree_end` (mods/coop/tribe.cpp away_tree_end_tick): in the frame the guest sees the host back, every
mirror's running behaviour tree (not a detached body, not a `localbt` test mirror, its tribe set) is ended with
Citizen_BTTerminate 0xC0D500 (as BUG-0667 does for bodies): the nodes' exits run while what they hold is still there,
and the next away starts the trees afresh, so no node goes on with a bundle the sync removed meanwhile. Counter
`away_trees_ended=` in the `tribe(guest)` status line. Test hooks: `coop.awaytest 1|0` (coop.cpp: the host counts as in
an editor on this guest, the editor stand-in), `coop.tribe freezeat <id> <node> [state [ticks]]` (a mirror's tree held
in a node while the host is away). New symbols (GOG 2024): `SporeCoop::Citizen_BundleStep` 0xC25BB0,
`SporeCoop::BT_BundleNodeTick` 0xDBC730. `coop.fix tribe.away_tree_end off`: the trees stay frozen mid-node (0.7.13).

## Verification
Lab, `tribe_away_bundle` (tools/tests/coop_tribe_away_bundle.py, tribe fixture; check: the member's tree holds no
bundle the guest's game no longer lists; no faults):
- FAIL (`SPORE_LAB_FIXOFF="tribe_away_bundle=tribe.away_tree_end"`, lane 6): lab 20261113-182629 (node 04623E0C holds
  10FBBD50, listed 0 bundles, `removed=2`) and 20261113-182823 (holds 175EDB20, not listed). 0 caught faults in both.
- PASS (lane 7): lab 20261113-182632 and 20261113-182826 (the tree ended at the host's return, the bundle left on the
  ground and listed; 0 faults).
- Earlier set-ups that did not reach the node (kept for the record): the host's `coop.menu` + `coop.load` renumbers the
  members (20261113-174056); a gather order delivers inside the gather node (20261113-174359, -175118); the gift runs
  0x0462855E (20261113-180958); food in hand + held at state 10 or a ground bundle held at state 1 / 2: the node ended
  without a fault, fix off (20261113-181834, -182102, -182336).
REGRESSION_PLACEHOLDER

## History
- 2026-11-13: opened.
- 2026-11-13 (fix/w49-bundle): analysed (node 0x04623E0C state 3, frozen across the host's return) and fixed (tribe.away_tree_end); tribe_away_bundle FAIL x2 / PASS x2.
