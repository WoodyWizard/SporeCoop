---
id: BUG-0261
title: phase: the guest's console stops answering after the host pulls it back into Tribe (2 of 7 runs)
status: fixed
kind: test
stage: Tribe
reported: 2026-10-29
source: lab 20261029-051251 (main eea6b46)
role: guest
versions: unreleased
fixed_in: 0.7.3.2
branch: fix/w9-tribe
commits: 0e9b31d 273080f
tests: coop_phase.py (suite phase), tools/tests/twoinst.py Monitor
signature: FAIL invariant: guest: the console stopped answering (crash?) while the guest draws on and no crash report exists
docs: docs/06-testing.md#testing-tribe-w9
related:
---

## Report
Full lab run 20261029-051251 (lane 3, cold): coop_phase.py passed every check (guest followed the host into Tribe, went back to Creature, was pulled into the host's phase again; 0 faults on both), then the invariant check failed: "guest: the console stopped answering (crash?)". The guest's game log ends normally (`Alice appeared in the world` after loading Zorispe.spo); no crash report was found. Reruns 20261029-055430: 3/3 OK. Lab history: phase failed 2 of 4 runs before (same message). Next: look for a hang of the guest right after its second world load (watchdog, wine process state), out/lab/20261029-051251/phase*.

## Root cause
Observed (lane 8, sporekit/console.cpp): SporeKit's console serves every client on ONE thread and
runs `SK_CMD_ANY_THREAD` commands inline, the waiting ones too (`wait.frames`, `shot`). coop_phase.py
calls `wait.frames 300 60000` on the guest after the join; meanwhile twoinst.Monitor's `frame` on its
own connection waits behind it, past its 10 s socket timeout, and the Monitor reported a crash. The
game drew on (no hitch over 0.3 s in its log), a new connection answered 0.3-15 s later. Not a freeze
of the game: a test-harness artefact. Same verdict from fix/w9-core (lane 4, machine load ~25):
4 of 7 runs of civ_other_world / tribe_other_world / share_retry (out/lab/20261030-002615,
-002941, runs around 20261030-0036..0110) had "the console stopped answering" with every check
passed, no crash report and the console answering right after; a 1 s poller beside a standalone run
saw no gap. Under load the 10 s socket timeout of the watcher is reached by a console that is slow
or busy with another client's waiting command, not by a dead game. With the Monitor's diagnostics: 3 of 6 runs (lab
20261030-003237: "TimeoutError, loading=False, answered again after 2.3-9.5 s").

## Fix
twoinst.Monitor: after a failed `frame` it asks a fresh connection for up to 30 s; when that answers
it goes on watching (line "busy, not a crash"); the frame check still reports a real freeze, a dead
console still fails. The event text now carries the exception, `loading` and the answer delay.
`SPORE_MONITOR_STRICT=1`: the old verdict. Not changed: SporeKit's console design (one thread).

## Verification
`coop_phase.py` (suite `phase`, lane 8):
* FAIL with `SPORE_MONITOR_STRICT=1`: lab run 20261030-004903 (3 of 4 runs: "the console stopped
  answering", answered again after 0.9-1.9 s).
* PASS: lab run 20261030-005710 (6 of 6; 3 of them logged "busy, not a crash").

## History
- 2026-10-29: opened.
- 2026-10-30: fix/w9-tribe: root cause (the console's single thread busy with the test's own wait.frames), Monitor fixed; kind test.
- 2026-11-01: released in 0.7.3.2.
