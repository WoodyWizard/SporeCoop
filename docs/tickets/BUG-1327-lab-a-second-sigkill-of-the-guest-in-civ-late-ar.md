---
id: BUG-1327
title: Lab: a second exit of the guest in civ_late_arrival (after its own Civ editors) loses the lane's wineserver: the host's game keeps running without its sockets
status: open
kind: tooling
stage: Tools
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch:
commits:
tests: civ_late_arrival
signature:
docs:
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

## Fix
Not fixed. Tried: the third join with the guest's own exit (`coop.close`, WM_CLOSE) instead of SIGKILL (lab
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

## History
- 2026-11-19: opened.
