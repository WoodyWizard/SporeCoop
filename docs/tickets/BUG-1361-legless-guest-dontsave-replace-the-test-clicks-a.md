---
id: BUG-1361
title: legless_guest_dontsave/_replace: the test clicks a save dialog that BUG-1255 removed for a guest on the world's species (test bug)
status: fixed
kind: test
stage: Creature
reported: 2026-11-20
source: final check 20261120-133703
role: guest
versions: 0.7.15.2..0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w61-creature
commits: d5d44e38
tests: legless_guest_dontsave, legless_guest_replace, legless_guest (coop_legless_guest.py --guest dontsave|replace|new)
signature: legless_guest_dontsave FAIL "guest (dontsave): it plays the host's evolved species, not the legless one" with the guest on a new own key (292AA448, the host shows 7C300BFB)
docs:
related: BUG-1255, BUG-0278, BUG-0756
---

## Report
Second final check of 0.7.16 (rel/0.7.16 673f7a55, out/lab/20261120-133703, lane 6): legless_guest_dontsave FAIL
"guest (dontsave): it plays the host's evolved species, not the legless one, on its own screen and on the host's" and
the same after the rejoin. The failing line names what the test WANTED (the host's species), not what happened: the
guest played its own new creature. The final check's brief read it the other way round ("the guest plays the host's
evolved species instead of its own legless one"); that is not what the logs show.

## Root cause
Test bug: the test was written for the 0.7.10..0.7.15.1 own editor and was never run after BUG-1255 (released in
0.7.15.2) changed it on purpose.
* Observed (guest game log of the run): `own editor result: tag 00000000 remaining DNA 65 cancelled 0` (the edit was
  kept and charged 50 DNA), `creations: own creature 292AA448 recorded for world 9652F8AD`, `sending our creature
  292AA448: 4 record(s), 1 model(s)`; the host's game then shows the guest as 7C300BFB (its copy of 292AA448). The
  test's own state line: guest_avatar 292AA448, host_shows 7C300BFB, k1 (the host's evolved) 292AA44B.
* Observed (shot out/lane6/shots/legless_guest_dialog_dontsave.png of rel/0.7.16, 3 s after done): no save dialog, the
  game already plays the hatching scene of the guest's legged creature. The same in legless_guest_dialog_replace.png
  (lane 3). The 0.7.15 shot (rel/0.7.15 lane 4, legless_guest_dialog_new.png, 2026-11-17) still shows the dialog
  "Save 'Sporoo' as a new creation or replace 'Sporoo' before exiting?" with Don't Save / Cancel / Replace / Save New.
* Cause: BUG-1255 (creations.own_editor_no_replace): a guest's own editor that starts from the world's species (no own
  creation in play, as this guest after Cell) has no replace dialog, done saves a new creature. The test still clicked
  "Don't Save" at (510, 604), which then landed in the world; the edit stayed, so the guest correctly plays its own
  legged creature, and the test expected the host's species "as cancel". legless_guest_replace passed only by chance
  (its expectation, an own new creature, is what done gives now). The own-species line of 0.7.16 is not involved: the
  mod behaves as designed on both screens.

## Fix
Test only (tools/tests/coop_legless_guest.py, suite comments): after done in the first own editor the test checks
that no save dialog is up (save_dialog_up of coop_evo_screens.py, the golden buttons in the picture) and clicks the
dialog's button only when it is up; new / replace / dontsave all expect the guest's own new creature on both screens
(and after the rejoin). Not a loosened check: the old dontsave expectation described a dialog that no longer exists,
and the new "no dialog" check is stricter than before (BUG-1255's regression is caught here too). The next own editor
(the guest then plays its own creation) still has BUG-0756's dialog and is unchanged.

## Verification
Was waiting for the lab run (no game today, owner rule):
* PASS expected: `SPORE_DETECTOR=20 python3 tools/lab.py run legless_guest legless_guest_replace legless_guest_dontsave
  legless_guest_done legless_guest_cancel --lane N --lane M --retry 0 --no-build`
* FAIL expected (the dialog check): `SPORE_FIX_OFF=creations.own_editor_no_replace` legless_guest_dontsave (the
  0.7.15.1 dialog comes up after done).
* The FAIL of the old test: out/lab/20261120-133703 (legless_guest_dontsave, lane 6).
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): legless_guest_dontsave PASS in 20261121-003202 and 20261122-194210 (legless_guest, _replace, _done, _cancel PASS there too); the twin SPORE_FIX_OFF=creations.own_editor_no_replace FAIL in 20261121-001418 ("done in the guest's first own editor asks nothing"). Closed.

## History
- 2026-11-20: opened.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
