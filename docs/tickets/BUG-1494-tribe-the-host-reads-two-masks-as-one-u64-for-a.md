---
id: BUG-1494
title: Tribe: the host reads two masks as one u64 for a guest's gather order (the members go to an empty tree) (TR-8)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w79-tribe
commits: 2b2171fa
tests: tribe_fruit_empty_tree, tribe_fruit_empty_tree_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.tree_mask
related: 
---

## Report
External static audit of 0.7.15.5, item TR-8 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-cr-tr.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-B1 (fix/w79-tribe). A behaviour change gets a switch and a twin.

## Root cause
* Code: tribe_host.cpp fruit_instead (BUG-0744) took the nearest tree within 12 m with `F<uint64_t>(t, 0x48) != 0`:
  +0x48 (the fruit left) and +0x4C (every fruit slot, 0x07 / 0x1F, never 0) read as one u64 (docs/re/tribe.md
  CORRECTION 2026-11-13: two u32 masks), so every tree passed and a guest's gather order on a fruit the host lacks went
  to the nearest tree even when it was empty: the members walked there and gathered nothing. for_each_tree_pos
  (tribe.cpp) already read the u32.

## Fix
tribe_host.cpp: `tribe_logic::tree_has_fruit` (+0x48 alone; unit test). Test hook `coop.tribe untree <x y z> <r>` (host:
the trees within r get +0x48 = 0). Switch `coop.fix tribe.tree_mask off`: the u64 read (0.7.16.1).

## Verification
open, waiting for the lab run. pending night run: tribe_fruit_empty_tree, tribe_fruit_empty_tree_off.
`coop_tribe_fruit_order.py --empty-trees` (suite `tribe_fruit_empty_tree`, tribe, warm): BUG-0744's case with every
host tree within 12.5 m of the fruit emptied; the order's target (the host's last order line, the member's order
record) is not one of them. Twin `tribe_fruit_empty_tree_off` (SPORE_FIX_OFF=tribe.tree_mask): the nearest empty tree,
must FAIL.
0.7.17 release lab: PASS: tribe_fruit_empty_tree (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.tree_mask, tests tribe_fruit_empty_tree, tribe_fruit_empty_tree_off); lab run pending (no lab by day).
- 2026-11-24: released in 0.7.17.
