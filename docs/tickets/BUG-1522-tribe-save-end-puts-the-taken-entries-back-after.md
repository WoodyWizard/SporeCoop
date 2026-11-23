---
id: BUG-1522
title: Tribe: save_end puts the taken entries back after a save without checking that each entry is still alive (CIV-1's bug in Tribe)
status: open
kind: crash
stage: Tribe
reported: 2026-11-23
source: plan
role: guest
versions: 0.7.16.1 and earlier
fixed_in:
branch: fix/w79-tribe
commits: 70979dc4
tests: tribe_save_took, tribe_save_took_off
signature:
docs: docs/re/tribe.md#tribe-audit-wp-b1
switch: tribe.save_took_listed
related: BUG-1416
---

## Report
tribe.cpp `save_end` (g_took_t) puts the entries it took out for a save back when the container is alive, but does not check the entry itself, which the host may have destroyed during the save window: BUG-1416 (CIV-1) in Tribe. Test like civ_save_took (a member removed by the host while the guest saves) and a twin. docs/plan-0.7.17.md item 12.

## Root cause
* Code (inferred, not run): save_start takes the spawned mirrors and whole made tribes' members out of every tribe's
  member (+0x340) and selectable (+0x354) vector; save_end (next frame, after that frame's network receive) put them
  back checking only that the tribe was alive. A host state in between that no longer names a mirror calls
  remove_member: with tribe.member_lifetime on, tribe_holding finds no tribe (the mirror is in no vector), the removal
  is refused and the mirror forgotten, and save_end puts it back: a member of the guest's player tribe no mirror is
  bound to (standing there for good once the host's member is really gone; while the host still sends it the next
  snapshot binds it again). With member_lifetime off, RemoveMember(c, destroy) freed it and save_end wrote a freed
  pointer back into both vectors (BUG-1416's crash shape). The list was not cleared at a world change or session end.

## Fix
tribe.cpp / tribe_mirrors.cpp: each taken entry keeps its citizen and serial; remove_member first calls `took_return`
(a member removed during the window goes back into its vectors, then RemoveMember removes it as always); save_end puts
an entry back only when its tribe is still in all_tribes(), the citizen is listed with the same serial and not in the
vector already (`tribe_logic::took_goes_back`, unit test); on_world_change / on_session_end clear the list. Status
`save_took: taken= back= gone= returned= cleared= killed= armed=` (`coop.tribe`, `coop.tribe tribelists`). Test hook
`coop.tribe savekill on` (guest): the next save's end first removes one taken mirror as guest_state's sweep does, and
the host's states skip that member from then on (tests only, cleared with the world and the session). Switch `coop.fix
tribe.save_took_listed off` (unsafe): every entry back, the removal refused (0.7.16.1).

## Verification
open, waiting for the lab run. pending night run: tribe_save_took, tribe_save_took_off.
`coop_tribe_save_took.py` (suite `tribe_save_took`, tribe, warm): the host spawns 3 members, the guest arms savekill and
saves; PASS: killed=1, `coop.tribe tribelists` unlisted=0 and no new orphan in the guest's player tribe, no fault for
15 s. Twin `tribe_save_took_off` (SPORE_FIX_OFF=tribe.save_took_listed): an orphan more, must FAIL.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 12 (WP-B1), branch fix/w79-tribe.
- 2026-11-23: fix on fix/w79-tribe (switch tribe.save_took_listed, tests tribe_save_took, tribe_save_took_off); lab run pending (no lab by day).
