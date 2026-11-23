---
id: BUG-1327
title: Lab: a second exit of the guest in civ_late_arrival (after its own Civ editors) loses the lane's wineserver: the host's game keeps running without its sockets
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in: 0.7.16
branch: fix/w58-wineserver
commits: 7fd8a094 d858be0b
tests: lane_guest_restart, lane_guest_restart_off (twin), civ_late_arrival
signature:
docs: docs/06-testing.md#testing-lane-pid-namespace
related: BUG-1290, BUG-1289
---

## Report
civ_late_arrival died at its last step ("once per world: a second rejoin offers nothing") with a traceback: the host's
console timed out in wait_guest_in after the guest's second restart (lab fix/w57-own-editors 20261119-122505 and
-130049, and a manual run on lane 2). The host's game log ends at the guest's kill, without a fault or crash report.

## Root cause
✔ (runtime, lane 2 after the manual run) The host's SporeApp.exe (instance 3) still ran (34 threads, sleeping) but its
console port 47802 was no longer listening and the bottle SporeLab2 had a new wineserver, started by the guest's
relaunch: the old wineserver, which held the host's sockets, was gone after the guest's SIGKILL (kill_game). The first
SIGKILL of the same test (before any own editor) did not do it; a probe that killed a guest whose own editors had not
opened did not either. Not a bug of the mod (a SIGKILL'd Wine client taking its wineserver along); why only after the
guest's own Civ editors is not known.

~ superseded (2026-11-19): see the CORRECTION below.
CORRECTION (2026-11-19, fix/w58-wineserver): ✔ runtime, lane 3: not the own editors, not the kind of exit, and not a
bug of the mod. Two plain kill and restart cycles of the guest's game (galaxy menu, no session, no mod action)
reproduce it every time. Every `spore.sh run` is a flatpak sandbox with a PID namespace of its own; the lane's
wineserver lives in the namespace of the first launch (the guest's first game: twoinst.start starts the guest first)
and keeps each game's unix pid as that game sees it in its own namespace. Fresh sandboxes number their processes alike
(the host's and the guest's game were both pid 49 of their namespaces, the wineserver pid 47 of the first one). At the
exit of the restarted guest (its own sandbox) the wineserver called `kill(47, SIGKILL)`: in its own namespace, itself
(gdb attached to the wineserver, `catch syscall kill`: `kill(49, 0)`, `kill(47, 0)`, `kill(45, 0)`, then `kill(47, 9)`
and "Program terminated with signal SIGKILL"). The first kill is harmless: that game was in the wineserver's own
namespace, its pids were right. Details: docs/06-testing.md#testing-lane-pid-namespace.

## Fix
fix/w58-wineserver: `tools/spore.sh run` starts a game of a lane whose wineserver runs inside that wineserver's PID
namespace (`flatpak run --parent-pid=<the lane's wineserver> --parent-share-pids`; `spore.sh wineserver` prints it, the
wineserver whose WINEPREFIX is the lane's bottle); the first game of a lane makes the namespace as before. Every game of
the lane has its real pid in the wineserver's eyes. `SPORE_SHARE_PIDS=0`: a sandbox of its own per launch (the old
way, the twin). Lab self-check `lane_guest_restart` (three guest kills and restarts while the host plays on). No change
of the mod.

Earlier attempt (fix/w57-own-editors, kept for the record): tried the third join with the guest's own exit (`coop.close`, WM_CLOSE) instead of SIGKILL (lab
fix/w57-own-editors 20261119-142554 and a manual run with a log watcher): the guest's game ended in 1 s and the host
was lost all the same (log still, console refused at once, a new SporeLab2 wineserver started with the guest's
relaunch at 15:02:28). So the trigger is the guest's process leaving the shared wineserver of the lane the second time,
not the kind of kill; the change was taken back. Next: look at which sandbox (flatpak / bwrap process tree) starts the
lane's wineserver after the first guest restart (if the restarted guest's sandbox owns it, its exit takes the host's
server along), e.g. `ps -o pid,ppid,lstart` of the wineserver and the games before and after each restart; or run the
test's restarts with `spore.sh` starting the guest outside a new sandbox.
## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- FAIL without the fix: lane_guest_restart_off (`SPORE_SHARE_PIDS=0`) in fix/w58-wineserver worktree
  out/lab/20261119-165314 (the games not in the wineserver's namespace; after the second kill the host's console
  refuses, the wineserver is gone); civ_late_arrival in fix/w57-own-editors out/lab/20261119-122505, -130049, -142554
  (its third join timed out).
- PASS with the fix: lane_guest_restart in out/lab/20261119-165314 (three kills, one wineserver, the host answers);
  civ_late_arrival in out/lab/20261119-165546 (the third join works, its last step "a later join of this world offers
  the editors no more" passes: why 'offered in this world already').
- Manual: lane 3, three kill and restart cycles each way: with the shared namespace one wineserver all along, the
  host's console up; with `SPORE_SHARE_PIDS=0` the wineserver gone at the second kill, a new one at the next launch.

## History
- 2026-11-19: opened.
- 2026-11-19: fix/w58-wineserver: cause found (the wineserver SIGKILLs its own pid for a game of another PID namespace), lane games share the wineserver's PID namespace; self-check lane_guest_restart; civ_late_arrival PASS to its end. Fixed.
- 2026-11-19: on the tree merged with rel/0.7.16 (1d5f3532): lane_guest_restart PASS, lane_guest_restart_off FAIL, civ_late_arrival PASS (out/lab/20261119-171000).
- 2026-11-23: released in 0.7.16.
