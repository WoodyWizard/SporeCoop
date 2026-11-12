---
id: BUG-0956
title: Space: a guest that earned a badge faults when it loads the host's world at a rejoin from the same game (a call into freed memory)
status: fixed
kind: crash
stage: Space
reported: 2026-11-12
source: lab, fix/w48-reports (coop_space_badges_rejoin.py)
role: guest
versions: 
fixed_in: unreleased
branch: fix/w48-new
commits: b5c3735c, 6d1517c6
tests: dead_modal_list, space_badges_rejoin
signature: guest, Space rejoin load: 0xC0000005 / 0xC0000096 / 0xC000001D at a changing address 0x18..0x20 million (not the exe), edx=0x00420065, ebp=01414BA0, 4 ms after "world: loading Madas.spo from ...Game2"
docs: docs/re/space.md#space-modal-list-crash, docs/02-modkit.md#modkit-offstack-report, docs/06-testing.md#testing-modal-list-owners
related: BUG-0786, BUG-0678, BUG-0270
---

## Report
Lab, fix/w48-reports (coop_space_badges_rejoin.py, 2026-11-12, lane 4, `space` fixture). The guest earns a badge (by the
game's own Update after `coop.space badges event 2 60`, or by GiveBadge from the console), leaves the session and
joins again from the same running game; the host shares its world; the guest quits to the galaxy menu ("leaving the
stage to the galaxy menu before loading Madas.spo"), loads the received world from slot Game2 and faults 4 ms after
the load starts: `exception 0xC0000005 at 0x1F7441E4 (data 0x00420065)`, other runs 0xC0000096 (privileged
instruction) / 0xC000001D (illegal instruction) at other addresses in the 0x18000000..0x20400000 range, always
edx=0x00420065 and ebp=01414BA0: the game executes or calls through freed memory. The game dies without a crash
report. Seen in 9 of 10 such runs, with `coop.fix space.keep_badges` on and off (runs 20261112-041609, -042344,
-042734, -043458, -044420, -044524, -044626); one run without the fault (20261112-042130).
Not seen: the same guest closed and started again before it joins (the test's default since then, 6 runs); a host
that earned a badge and loads its own save from the galaxy menu (coop.menu + coop.load, lane 5); the hold test
space_items_rejoin (no badge earned).

## Root cause
Observed (fix/w48-new, lane 4; docs/re/space.md#space-modal-list-crash): after the load the mode switch from the galaxy
menu (cGameModeManager 0x7D8670) pops the modal window; cWindowManager::PopModal 0x959410 (WM vt 0x78) makes the first
window of the list of modal windows (+0x81C) the modal one and calls its vt 0x10 at once. That window was already
destroyed (vtable 0x13EB92C, the refcounted base; its slot 0x10 reads 0x00420065, the tail `mov esp,ebp; pop ebp;
ret 4` of 0x420050): esp moved into the heap (ebp was the popped window), and the game ran heap bytes until a fault at a
changing address (edx=0x00420065, eax=0x1440D0C, edi=esi+0x81C at every fault). With the fix's guard the destroyed
window (vtable 013EB92C) was found in the list in every run (4 of 4, lab 20261112-073016 .. -073346).
Inferred (not identified): the destroyed window is a modal window the badge's earning put up (its caption / card) and
the Space UI destroyed at the stage exit without PopModal; a restarted game has none. Not the mod's badge module.

Why no crash report: the fault's esp was outside the thread's stack, and Wine refuses to dispatch such a fault
("err:seh:NtRaiseException Exception frame is not in stack limits => unable to dispatch exception." in the lane's
out/run.8.log) and ends the process: the game's handlers, SporeKit's unhandled-exception filter and the ExitProcess hook
never run. In most runs the vectored handlers still ran (SporeKit's fault logger wrote the exception line); in about
1 of 4 Wine refused even that (no line at all: nothing can catch it).

## Fix
* `coop.fix game.dead_modal_list` (gamefixes.cpp, hooks PopModal; also before UpdateHover): entries of the list +0x81C
  whose window is not alive (the BUG-0270 check: vtable slots 0xC4 / 0xF8 code) are unlinked before PopModal runs.
* `kit.fix offstack` (SporeKit exceptions.cpp / diag.cpp): a fault whose esp is outside its thread's stack writes the
  crash report (archive, minidump) from the fault logger through the report thread made at boot, and logs the
  return addresses of the thread's real stack around its innermost SEH record (this found the cause). The case where
  Wine refuses the dispatch before the vectored handlers cannot be caught.
* `coop.msglisteners [id]` lists the game's message listeners (RE).

## Verification
* dead_modal_list (the state put in directly, then the game's PopModal): FAIL lab 20261112-072743 (lane 5,
  SPORE_FIX_OFF=game.dead_modal_list: the guest faults at 0x65), PASS lab 20261112-072746 (lane 4).
* The real game, `BADGES_RESTART=0 space_badges_rejoin`: SPORE_FIX_OFF=game.dead_modal_list FAIL 4 of 4 (lab
  20261112-073016-2, -073122, -073232, -073337, lane 5; the last three with the crash report of kit.fix offstack);
  with the fix PASS 4 of 4 (lab 20261112-073016, -073127, -073239, -073346, lane 4), each with the guard's line "a
  destroyed window ... (vtable 013EB92C) was listed under the modal window (PopModal): dropped it".
* The crash report: before kit.fix offstack none (lab 20261112-064427); with it crash-*.txt + .dmp + archive (lab
  20261112-065104, -065642).

## History
- 2026-11-12: opened (fix/w48-reports, met while testing BUG-0786).
- 2026-11-12: cause found and fixed on fix/w48-new (a destroyed window in the modal list; PopModal guard; SporeKit writes a report for an off-stack fault).
