---
id: BUG-9301
title: Civ: a guest's save puts back a mirror vehicle the host's state removed during the save (dangling pointer in the civilization's vehicle lists, CIV-1)
status: open
kind: crash
stage: Civ
reported: 2026-11-22
source: audit
role: guest
versions: 0.7.16 and earlier (since BUG-0470, 0.7.3)
fixed_in:
branch: fix/w63-civ
commits:
tests: civ_save_took, civ_save_took_off
signature:
docs: docs/re/civ.md#civ-mirror-vehicle-reuse
related: BUG-0470, BUG-0478, BUG-0951, BUG-0264
---

## Report
External static audit of 0.7.15.5, item CIV-1 (out/research/ext-audit-0.7.15.5.md; verified in
out/research/ext-audit-verify-civ-spc.md). Not seen on a player's PC; rare: a guest's pause-menu save in Civ, or the
quit message's save when a guest leaves Civ to load a shared world (BUG-0264), while a host vehicle dies or is
disbanded during the save hitch.

## Root cause
* Code (rel/0.7.16): `civ_guest.cpp` save_start (run by savestrip inside the save's first COMSerializer::Write) takes the
  mirror vehicles out of every civilization's vehicle vector (+0xB0) and selectable vector (+0xD4) into `g_took`;
  save_end runs only from `savestrip::tick()` in the next frame's on_frame, AFTER that frame's network receive.
* A host MSG_CIV_STATE in that frame whose complete cycle no longer names a mirror calls `kill_local` (Vehicle_Remove
  0xCAA7B0: out of the game's list at once, freed later, docs/re/civ.md#civ-mirror-vehicle-reuse). save_end then
  wrote the removed vehicle back (`*e++ = t.p`): a dangling pointer the civ update walks once the block is freed.
* `on_world_change` / `on_session_end` did not clear `g_took` (the world-change half is theoretical: BUG-0264's
  measurements show our frame runs right after the save while the stage still lives). Inferred, not run.

## Fix
civ_guest.cpp: save_start keeps each taken vehicle and its serial (cGameData::mID); save_end puts an entry back only
when its civilization is still in GetAllCivs and the vehicle is still in the game's vehicle list as the same object
(`exists()` read fresh, serial equal; rule `civlogic::took_goes_back`, tests/civ_tests.cpp). The taken list is cleared
by on_world_change and on_session_end (`took_clear`). Counters in `coop.civ tables` (`save_took: taken= back= gone=
cleared=`), `coop.civ civlists` prints `unlisted=` (entries the game's list no longer has). Test hook `coop.civ
savekill on` (guest): the next save_end first removes one taken mirror, as the host snapshot would. Switch
`coop.fix civ.save_took_listed off` (unsafe: every taken entry goes back, 0.7.16).

## Verification
open, waiting for the lab run. `coop_civ_save_took.py` (suite `civ_save_took`, civ_npc, warm): the host buys
vehicles, the guest arms savekill and saves; PASS: the save's end ran with the removal, `civlists` unlisted=0 on the
guest, no fault for 15 s. Twin `civ_save_took_off` (SPORE_FIX_OFF=civ.save_took_listed): unlisted >= 1 or a fault,
must FAIL.

## History
- 2026-11-22: opened.
- 2026-11-22: fix on fix/w63-civ (switch civ.save_took_listed, test civ_save_took and its twin); lab run pending.
