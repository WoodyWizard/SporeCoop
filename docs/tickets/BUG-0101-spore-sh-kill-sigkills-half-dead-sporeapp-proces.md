---
id: BUG-0101
title: spore.sh kill SIGKILLs half-dead SporeApp processes of every lane, not only its own
status: fixed
kind: tooling
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#79
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits: 14463ee
tests: tools/tests/tools_selftest.py kill (make test)
signature:
docs:
related: #79
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/79 (full text, discussion).

`spore.sh kill` is meant to stop the games of one lane (`SPORE_LANE`). Its zombie step sends SIGKILL to every `SporeApp` process in state Z on the machine, whatever lane or install it belongs to. The comment explains the step is for "a deadlocked Wine process [that] can stay half-dead (zombie leader + live threads)": for exactly that kind of process, SIGKILL to the leader kills the live threads too. So a lane's kill (which the lab runs after every failed test) can kill a hung game of another lane while that lane's test is still recording the freeze, or the owner's own Spore.

### Where
https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/tools/spore.sh#L173-L181

### How to reproduce
Not reproduced (needs a half-dead Wine process on one lane while another lane runs `spore.sh kill`). By reading: run any lane's `SPORE_LANE=2 tools/spore.sh kill` while lane 3 has such a process; the awk filter selects lane 3's PID because it only looks at `comm` and the state.

### Expected
`spore.sh kill` only touches processes of its own lane (`$GAME_RE`, its bottle).

## Root cause
**What happens**

```bash
pkill -f "$GAME_RE" || true          # lane-scoped (the lane's folder in the Windows path)
sleep 1
pkill -9 -f "$GAME_RE" || true       # lane-scoped
for z in $(ps -eo pid=,stat=,comm= | awk '$2 ~ /Z/ && $3 ~ /SporeApp/ {print $1}'); do
    kill -9 "$z" 2>/dev/null || true # NOT lane-scoped: comm only
done
```

`comm` is `SporeApp.exe` for every lane, for lane 1 (the real install) and for any Spore the owner started. A fully dead zombie ignores the signal, so nothing visible happens in the common case. A thread group whose leader exited while other threads still run (the case the comment names) is killed. That is the state a hung game can be in while another lane's `twoinst.Monitor` is waiting to report `FROZE` and the SporeKit watchdog is writing the stack.

### Confidence
PLAUSIBLE (the selection is CONFIRMED by reading; the cross-lane kill needs the half-dead state).

## Fix
The zombie loop by `comm` is gone. A half-dead game (zombie leader, live threads) has no command
line on the leader, so `lane_procs` reads a live thread's `/proc/<pid>/task/<tid>/cmdline` and
matches the lane's folder like any other game of the lane; then the leader gets SIGKILL (the whole
thread group). Another lane's half-dead game does not match.

## Verification
`tools/tests/tools_selftest.py kill`: a Python fake that exits its main thread with `syscall(SYS_exit)` while a second
thread sleeps (state Z, comm `SporeApp.exe` via prctl), one under "lane 2" and one under "lane 3";
`SPORE_LANE=2 spore.sh kill`.
- FAIL with main's spore.sh: "lane 2 kill leaves the half-dead game of lane 3 alone" FAIL (and lane 1 kill also killed it).
- PASS with the fix: lane 2's half-dead game ends, lane 3's survives.

### Related
docs/tickets/BUG-0025 (kill not PID-scoped on lane 1); CLAUDE.md section 3 ("Lanes are independent copies of the game + bottle").

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #79.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w4-tools (wave 4, tests and tooling).
- 2026-10-28: released in 0.7.3.0.
