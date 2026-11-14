---
id: BUG-1023
title: UI: notify::shutdown leaves queued outside-frame tasks and the queue; g_last_frame read outside the lock
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-7
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-kit2
commits: 702f51dd
tests: notify, hotreload
signature:
docs:
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding SPC-7 (lowered by the audit's own verifier from High to Low). Not seen at
runtime, found by code reading. On mod.unload / mod.reload the toast module's shutdown removes the visible panels but
keeps the queue and the "task queued" flags, and an already queued outside-frame build / animate task can run after
it; g_last_frame is a plain double written by the main thread and read by show() under g_mu only by contract.

## Root cause
Observed in code (main efd2e0ba), confidence high for the code, medium for the effect:
* mods/coop/notify.cpp:199 shutdown() only removes g_toasts; g_queue, g_build_queued and g_anim_queued (:19, :20)
  stay; a build (:73) or animate (:131) task queued with run_outside_frame (:179, :190, :194) before the unload runs
  later. After a reload the old DLL's code would run (the unload path of KIT-1 / SPC-6, other tickets), within one
  DLL it rebuilds toasts after shutdown.
* :21 g_last_frame (double) is written in tick (:185, main thread, no lock) and read by frames_running() in show()
  (:177, under g_mu). Every current notify::show caller (coop.cpp:350, coop_ui.cpp:661, :766) is on the main thread
  (inferred from their callers), so no live race.
Only the debug unload / reload path reaches the first part; the second is a contract. Severity: Low.

Observed vs inferred (2026-11-13, fix/w49-kit2): observed in code; a panel rebuilt after the unload was never seen (the old image's queued tasks are also dropped by SporeKit's BUG-0060 drop_tasks_in, which leaves only the in-image case).

## Fix idea
shutdown(): under g_mu clear g_queue and set a `g_closed` flag that build/animate check first; make g_last_frame a
std::atomic<double> (or read it only on the main thread). Pair with the unload barrier of the KIT-1 ticket.

## How to test
tools/tests/coop_notify.py: queue 5 toasts, then `mod.reload coop` in the same pump; assert no toast window from
before the reload is in the UI tree after it (coop.ui find) and no fault. FAIL on main if the queued build runs after
shutdown, PASS with the flag.

## Fix
mods/coop/notify.cpp: shutdown() takes g_mu, sets g_closed, clears the queue and the queued-task flags; build,
animate and show return at once while closed; init() opens it again. g_last_frame is a std::atomic<double>. No switch
(cleanup on the unload path only).

## Verification
no-fail-run: the queued build of the same image after shutdown needs the unload between the queueing and the
outside-frame pump (one message); SporeKit already drops the old image's queued tasks, so no failure could be made;
fixed by inspection. Regression: notify and hotreload (mod.reload with the toast module) passed out/lab/20261113-083757 (notify, hotreload, reload_update, kit_cmd_fault).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (no-fail-run, decision 4).
- 2026-11-14: released in 0.7.14.
