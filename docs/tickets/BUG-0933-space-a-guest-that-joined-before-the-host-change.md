---
id: BUG-0933
title: Space: a guest that joined before the host changed its save never gets the shared Space money (held as 'another Space save' for the rest of the session)
status: fixed
kind: bug
stage: Space
reported: 2026-11-11
source: final check 2 of 0.7.12, campaign (lab 20261111-090547)
role: guest
versions: 0.7.11, 0.7.11.x
fixed_in: 0.7.12
branch: fix/w47-final12b
commits: 340bd402
tests: space_save_change (coop_space_save_change.py); space_own_save; validate_tests (host_world host_save_now, announce_world)
signature:
docs: docs/07-modes.md#modes-host-save-follow
related: BUG-0709, BUG-0420, BUG-0231
---

## Report
The second final check of 0.7.12 (rel/0.7.12 lab 20261111-090547, campaign, four players, lane 2) failed on two
findings "[shared] Carol / Bob: shared money differs from the host's in space": the detector's `Space:state:money`
said 1 100 000 on the host and 100 000 on Bob and Carol from the first minute of the Space play, later 2 100 000 on Bob,
until the end of the stage (15 minutes). For a player: in Space the money of a guest stands still while the host
earns and spends, the guest's own earnings and purchases stay in its own game, and the two never meet again in the
session.

## Root cause
Observed (the four game logs of that run):
* Bob's and Carol's trace lines say `money=100000(unsynced)` from their first Space line to the last: shared.cpp's sync
  state never became `have` (the totals are held: `guest_space_other()`, BUG-0709 of 0.7.11). Dave's lines have no
  `(unsynced)` and its money follows the host's.
* The host's save name changed in the session: `Cadian.spo` (world 9652F8AD) in Cell, Creature and Tribe, `Tavael.spo`
  (BF000B7B) from the Civ stage on (the campaign's Tribe -> Civ step loads another save).
* The WELCOMEs: Bob's last at 09:33 and Carol's at 09:10 (the host's save: Cadian), Dave's at 09:59 in Civ (Tavael).
  `host_world::space_other` compared the guest's Space save (Tavael) with the host's save of the guest's WELCOME
  (BUG-0709: "the host's save is the one of the guest's join"): another save for Bob and Carol, for good.
* The money: the host's bot used the game's own cheat (play_space `cheat moreMoney`, +1 000 000) at 10:12:55; Dave got
  the total, Bob and Carol did not. Bob's own bot used it at 10:15:02 and 10:15:22 (2 100 000 in its own game, never
  sent). The gates need the difference in two checks in a row, so it took a grant on one side to show.
* Not a regression of 0.7.12: the same `(unsynced)` for the whole Space stage is in the first final check of today
  (rel/0.7.12 lab 20261111-044257: Bob; the host and Bob each used the cheat once, so the totals happened to be equal)
  and in the final check of 0.7.11 (rel/0.7.11 lab 20261108-114435: Bob and Dave; nobody used the cheat). It came
  with BUG-0709 in 0.7.11; the session settings, the B2 Space fixes and the planner fixes of 0.7.12 are not involved,
  nor the pause window or Dave's rejoin next to the first detector lines.
Reproduced by coop_space_save_change.py (the guest joins in Creature, the host loads its Space save, the guest
follows): `host save: known 68AC222A (welcome 68AC222A ...), mine 33DE75F6, space_other=1`, `held=1/23`.

## Fix
A guest follows the host's save instead of keeping the WELCOME value (`res.space_save_follow`):
* host_world.h `host_save_now`: the host's state in a gated stage (Cell, Creature, Tribe, Civ) names its save (the
  state's world is the save's CRC there): taken as the host's save. Works with a 0.7.11 host too.
* a new message `MSG_HOST_WORLD` (69, reliable, Session class; older builds ignore it): the host sends its world id
  (the value a WELCOME would carry) whenever it changed to another known one, and again after every join (a WELCOME
  sent while the host was in an editor names no world). This covers a host that goes from one Space save to
  another, where no gated state is seen.
* coop.cpp `guest_space_other` compares with the followed save; a new session starts from its WELCOME again.
`coop.fix res.space_save_follow off`: the WELCOME value for good and no announcement (0.7.11).
`coop.status` has a line `host save: known ... (welcome ..., followed ..., heard N), mine ..., space_other=..`.

## Verification
Test `space_save_change` (tools/tests/coop_space_save_change.py, fixture creature + the space fixture in slot 9):
the game's own money on both machines after the host earns 5000 and after the guest earns 700.
* FAIL: out/lab/20261111-110127 (`SPORE_FIX_OFF=res.space_save_follow`, lane 2): host 105000, guest 100000; the
  guest's 700 never reach the host. Also rel/0.7.12 out/lab/20261111-090547 (campaign, before the fix).
* PASS: out/lab/20261111-110129 (lane 4), out/lab/20261111-110356 (lane 4), out/lab/20261111-110525 (lane 2).
* campaign_space x2 out/lab/20261111-111643; campaign_short, campaign_space, campaign_ct, campaign_tribe OK in
  out/lab/20261111-113158. The full campaign of that run (the save changes from Cadian.spo to Tavael.spo at 12:23)
  has no `Space:state:money` line and no `[shared]` finding, and every guest's Space trace lines are synced after
  the first one or two (110, 103 and 106 lines); it failed on another problem (BUG-0942, a fault storm in Tribe).
* BUG-0709's test still passes: space_own_save out/lab/20261111-110356-4; detect_space out/lab/20261111-110531.

## History
- 2026-11-11: opened.
- 2026-11-11: fixed on fix/w47-final12b (res.space_save_follow).
- 2026-11-11: released in 0.7.12.
