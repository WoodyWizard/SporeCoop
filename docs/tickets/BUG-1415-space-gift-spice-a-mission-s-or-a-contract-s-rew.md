---
id: BUG-1415
title: Space: gift spice, a mission's or a contract's reward tool and Steve's gift reaching a rejoined guest before its own cargo hold is back are lost; Steve's gift is given in a guest's own Space save (audit SPC-8)
status: fixed
kind: bug
stage: Space
reported: 2026-11-22
source: audit 0.7.15.5 SPC-8 (out/research/ext-audit-verify-civ-spc.md)
role: guest
versions: 0.7.16.1
fixed_in: unreleased
branch: fix/w63-space
commits: ca07b2e8, b8c8196d
tests: space_grant_restore, space_grant_restore_off, space_core_own_save, space_core_own_save_off
signature:
docs: docs/re/space.md#space-restore-window
related: BUG-1034, BUG-0678, BUG-0694, BUG-0226, BUG-0709, BUG-0816
---

## Report
External static audit of 0.7.15.5, finding SPC-8 (verified REAL, progress loss). Not seen at runtime, found by code
reading. A rejoined guest's hold is the host's copy until its own kept hold comes back (BUG-0678, K_INV_KEPT, up to
20 s); BUG-1034 made only the trade gifts and sales (K_TOOL, K_SELL, answer_offer) wait. Grants of other files in that
window are lost: a partner's gift spice, a partner's mission reward tools, a contract's reward tool, and Steve's gift
(the Staff of Life) for good, because its flag kFlagCoreTools then blocks a second gift. Also reward_tick has no
own-save gate: a partner's core entry gives the Staff in a guest's own Space save.

## Root cause
Inferred from code (rel/0.7.16 5376d388), confidence high for the paths, medium that apply_inv drops the item (it
writes the kept hold over the current one, as BUG-1034 assumed and its test showed for gifts and sales):
* spacecoop.cpp on_gift K_GIFT: add_spice at once (only `!in_space()` waits).
* spacecoop.cpp K_MISSION: a_AddItemByKey at once (owed only when `!inv || !in_space()`, BUG-0694).
* space_contracts.cpp on_result: give_tool at once (owed only outside Space); owed_tick pays as soon as an inventory exists.
* space_finale.cpp reward_tick / give_reward: every 3 s from the first Space frame, MessageSend kMsgCoreTools.
* space_trade.cpp apply_inv takes out every item not in the kept hold (`if (!w) hit = i, take = -1`).
* reward_tick reads g_reached_by (the session's entries) with no own_world::space_other() check.

## Fix
One helper, `spaceext::hold_grants()` (space_trade.cpp, declared in space_ext.h): true while inv_restore_pending().
Its four users wait: K_GIFT is queued (g_gifts_wait, handled by gift_owed_tick after the restore in the same Space
world; K_GIFT_BACK goes to the owed list, which also waits), K_MISSION tools and a contract's tool are owed
(BUG-0694's list, owed_tick keeps them while the restore is pending), give_reward keeps Steve's gift
(g_reward_held, given by reward_tick after the restore in the same Space world). Switch
`coop.fix space.grant_wait_restore off`: given at once (0.7.16).
reward_tick ignores the partners' entries while own_world::space_other() (the guest's own Space save; its own save's
entered flag still counts). Switch `coop.fix finale.gift_own_save off`: 0.7.16.
Test knobs: `coop.space missiontool <hash>` (sends K_MISSION with that reward tool), `coop.space finale reached`
(sends K_REACHED without the trip). Status: `trade` grant_waits=, `gift` wait= waited= wait_dropped=, `finale`
reward_waits= reward_held= reward_other_save=. No wire change.

## Verification
Verified on lanes 2-3 (rel/0.7.16.1 `b8c8196d`, lab `20261123-144817`):
* `space_grant_restore` PASS (54 s), twin `space_grant_restore_off` FAIL (107 s: gift spice, mission tool, contract tool and Staff of Life all lost).
* `space_core_own_save` PASS (49 s, `restore_pending=0`, `reward_other_save > 0`, no Staff of Life), twin `space_core_own_save_off` FAIL (49 s: Staff of Life and core tools flag granted in the guest's own save).

## History
- 2026-11-22: opened.
- 2026-11-22: fix and tests on fix/w63-space (no lab run yet: lanes busy with the 0.7.16 final check).
- 2026-11-22: renumbered from BUG-9300 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9300.
- 2026-11-23: fix/w69-triage2: the twin space_core_own_save_off PASSED (20261123-120656) because in both arms reward_tick never looked at the partner's entry; updated test to wait for restore_pending=0 and report INCONCLUSIVE when undecided.
- 2026-11-23: rel/0.7.16.1 (`b8c8196d`): running the updated test (`20261123-144108`) showed `restore_pending=1` stayed true forever in an own Space save (inv_tick returns at inv_own_save() without sending a fresh hold, so g_win.pending saw `world != fresh` indefinitely). Returning false from `inv_restore_pending()` when `inv_own_save()` is true resolved it; verified all 4 tests/twins in `20261123-144817`. Closed.
