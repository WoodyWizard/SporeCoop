---
id: BUG-1374
title: Tests: chaos_civ_long: the chaos bot cannot leave a guest's new empty design editor (0.7.16 own Civ editors), no crash
status: fixed
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w61-civ
commits: ab8a6e1e, 34cb98e5, a22b1ccf
tests: chaos_civ_long (tools/tests/coop_chaos.py)
signature: FAIL the games survived the chaos / !! guest did not get back into the host world
docs:
related: BUG-1275, BUG-1158
---

## Report
Second final check of 0.7.16 (lab 20261120-133703, lane 3, cold, seed 919811266): chaos_civ_long `FAIL the games
survived the chaos` with `!! guest did not get back into the host world` after a churn (leave + join). Not a crash:
no fault on either game (`total faults: 0`), no crash report, no game died (results.json: faults, reports and died
empty); the triage-crash path does not apply.

## Root cause
Observed (guest log): a random click of the bot opened the guest's palette "new design": `civedit: our design editor
request (editor 8707BE7D, design 47C10953) starts from 00000000 (was 00000000, BUG-1275)`, `Civilization -> Editor`
at s 390. 48 s later the churn left and joined again; the guest, still in its editor, got the host's world with "the
world of Alice is loaded when you leave the editor" (by design). The bot's leave_editor pressed "done" four times (the
dice twice): the editor stayed (`gstage Editor` to the end). A new design starts as an empty creation, which "done"
does not save (coop_civ_guest_editors.py drags three parts first for the same reason). Before 0.7.16 a guest's design
editor was refused (BUG-1158), so the bot never met an empty editor on the guest; 0.7.16 (BUG-1275) opens it on
purpose. A player in that editor adds parts or leaves it with its own buttons: no mod fault. The bot's way out was
too short.

## Fix
Test only (coop_chaos.py leave_editor): up to six tries; in the second and fifth three parts are dragged in first
(coop_campaign's TRIBE_EDITOR_PARTS, drag_slow and editor_click: the frame-paced clicks of the campaign's editor
driver), the dice's name from the third; a screenshot `chaos_<who>_editor_stuck.png` when it is still in an editor.
The verdict rule is unchanged (the guest must get back into the host's world).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL (the bot stuck, before): 20261120-133703:chaos_civ_long.
- no-fail-run: a test bot change. Was waiting for the lab run: the replay
  `SPORE_LANE=3 python3 tools/tests/coop_chaos.py --fixture civ --minutes 8.0 --seed 919811266 --churn 1` (the same
  actions up to the churn) must get the guest out of its editor and PASS; chaos_civ_long must PASS.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): chaos_civ_long PASS in 20261121-003202 and 20261122-194210. Closed.

## History
- 2026-11-20: opened.
- 2026-11-20 (fix/w61-civ): triage (no crash), the bot leaves an empty editor; no lab run (owner rule).
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
- 2026-11-23: released in 0.7.16.
