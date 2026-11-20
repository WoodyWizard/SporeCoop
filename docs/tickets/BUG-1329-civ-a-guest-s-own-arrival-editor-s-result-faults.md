---
id: BUG-1329
title: Civ: a guest's own arrival editor's result faults at 0x645E4D in the Tribe goal listener
status: fixed
kind: crash
stage: Civ
reported: 2026-11-19
source: lab
role: guest
versions: 
fixed_in: unreleased
branch: fix/w58-assignnames
commits: 3afe08a4, ad2b5683
tests: civ_late_arrival, civ_late_arrival_tag_off, creation_props_key0, civ_own_arrival
signature: 0x00645E4D hash_map__int_int__Find+0xD < cIdentityColorable::AssignNames+0x1443 < TribeStage_RemoveMemberCard+0x1D8F < TribeGame_GoalListener+0x163
docs: docs/re/civ.md#civ-arrival-tribe-listener, docs/06-testing.md#testing-killed-faults
related: BUG-1276, BUG-1290, BUG-1328
---

## Report
Lab (found on the way by fix/w58-cityname, not fixed there): when the guest's own City Hall editor at its Civ arrival
(WP-X1, core.own_civ_arrival / core.own_civ_late) ends, the editor's result message 0x30C11C7 (tag 0x116DD1B, the end
of Tribe's) reaches SporeCoop::TribeGame_GoalListener 0xCD81F0, which is still listening in Civ, and the game takes a
first-chance access violation (read of 0x30) that the game survives under Wine:

    exception 0xC0000005 at 0x00645E4D hash_map__int_int__Find+0xD (data 0x00000030), ebp=030C11C7
    called from: Simulator::cIdentityColorable::AssignNames+0x1443 < SporeCoop::TribeStage_RemoveMemberCard+0x1D8F
    < SporeCoop::TribeGame_GoalListener+0x163 < App::cMessageManager::MessageSend

Seen: fix/w57-own-editors 20261119-113002 civ_late_arrival (a saved City Hall, lane 2), 20261119-130049
civ_late_arrival_unpause_off, fix/w58-cityname 20261119-200001 creation_props_key0_off and 20261119-200557
creation_props_key0 (both: the City Hall editor left with the red cross, lane 6). On civ_late_arrival's route (the guest's game restarted, joining the host's Civ world) every result of the guest's
own City Hall editor faulted (3 of 3 logs that hold one: 20261119-113002, 20261119-130049, and fix/w58-cityname
20261119-201729 creation_props_key0 with a saved City Hall, lane 6); civ_late_arrival itself passes because it restarts
the guest again afterwards: the lab keeps the last instance's log and fault count only. On civ_own_arrival's route (the
live end of Tribe) no run had it (20261119-200005 and the fix/w57-own-editors runs). The test's fault check fails the test. On Windows a first-chance AV in game code may
end the game (as BUG-0304's did).

## Root cause
(Ghidra, docs/re/civ.md#civ-arrival-tribe-listener) Our own arrival editors (civ_arrival.cpp open_editor) were built as
the end of Tribe builds them, with the Tribe tag 0x116DD1B at EditorRequest +0x90, which the editor copies into the
result's +0xC. The Tribe stage's message listener 0xCD81F0 is still registered in Civ and acts on every 0x30C11C7 with
that tag: cinematic manager, the Tribe hint 0x5312DA01 through 0xCD6B00 -> 0xB708A0 on the stage's hint list (stage
+0x394, ECX at 0xCD6F74), the result kept in stage +0x2F8 (and with +0x28 set its key copied into cPlayer +0x1150). In
Civ the hint list is null: 0xB708A0's hash_map Find reads +0x30 and faults at 0x645E4D. The stack's names
(AssignNames+0x1443, TribeStage_RemoveMemberCard+0x1D8F) are only the nearest symbols. Seen on both routes: the
restarted guest (civ_late_arrival, every run) and the live end of Tribe of creation_props_key0 (2 of 2); civ_own_arrival
and line_stages_editors had 2 results and no fault in each of 13 earlier runs (why the list is there in those games was
not analysed; the fix does not depend on it).

The lab missed it: civ_late_arrival restarts the guest twice and `Checks.faults()` asked only the games alive at the end
(the lab kept only the last instance's log), so the second instance's fault was never counted.

## Fix
* `coop.fix core.own_civ_tag` (civ_arrival.cpp): the guest's own arrival editors carry a tag of our own, 0x0C0B1329 (no
  dword of the exe has it), so no game listener acts on their results; `civ_arrival_result` takes the result by that
  tag. Off: the Tribe tag (the twin civ_late_arrival_tag_off). `coop.civarrival` shows `tag: fix=.. request=..`.
* Test: civ_late_arrival checks the guest's fault count around each own editor ("no fault at the end of its own ...
  editor").
* Harness: `twoinst.start()` sets `lane.on_kill`; every game a test kills after start (`lane.sh('kill')`,
  `kill_game`) has its `exc` read first and judged by `Checks.faults()` (`<who> (killed instance N) faults: ...`), its log
  copied as `<test>.<who>.killed<N>.game.log` (docs/06-testing.md#testing-killed-faults). `SPORE_KILLED_FAULTS=0`: the
  old way.

## Verification
(fix/w58-assignnames out/lab/)
* FAIL (twin, `core.own_civ_tag` off): 20261119-205546 civ_late_arrival_tag_off, lane 6: "the guest's game took no fault
  at the end of its own City Hall editor (0 -> 1 faults; 0x00645E4D ... stack ... 00B708C3 ... 00CD6F7F)", and the
  harness's new line `guest (killed instance 2) faults: total faults: 1` (FAIL lab: faults in the log).
* PASS: 20261119-205648 civ_late_arrival, lane 5: no fault at either editor's end, both results "from the editor's
  message" with tag 0C0B1329, `guest (killed instance 1/2) faults: total faults: 0`; key frames looked at.
* Same fault checked: 20261119-210144 civ_own_arrival and civ_guest_editors (lane 5), 20261119-210144-2
  line_stages_editors and creation_props_key0 (lane 6): all PASS, no 0x645E4D in any guest log, the arrival results
  taken from the editor's message (creation_props_key0 faulted in 2 of 2 runs before: 20261119-200557, -201729).

## History
- 2026-11-19: opened (fix/w58-cityname, found while testing BUG-1328; outside that task).
- 2026-11-19: fixed on fix/w58-assignnames (own request tag `core.own_civ_tag`; the harness counts killed games' faults).
