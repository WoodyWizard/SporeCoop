---
id: BUG-1194
title: monitor: drawn / hp rules matched a partner line with the subject's own line of another session of the same stage and world (sreport_leave)
status: fixed
kind: test
stage: Cell
reported: 2026-11-16
source: lab
role: 
versions: 
fixed_in: 0.7.15
branch: fix/w51-t5
commits: 5b2b7c55
tests: sreport_leave, gates_selftest
signature:
docs: docs/06-testing.md#testing-final-0715-t5
related:
---

## Report
Final check of 0.7.15 (lab 20261116-025154, rel/0.7.15 11114769, lane 3): `sreport_leave` failed on the monitor gate
`drawn: host draws guest 3.9 m from where guest's own game has it (limit 3.0 m, Cell) on 2 lines from s 20`. 5 of 6
runs on rel/main passed before. The test plays several sessions of the same Cell world: [A] one session, [B] the guest
leaves and joins again, [C] the host hosts again and the guest joins.

## Root cause
Not a game desync (observed in the run's logs): in every session the host's `drawn=` and the guest's own `pos=` of the
same moment agree (first session s 10 / 20: drawn (103.9 100.3) / (103.9 100.8), guest pos the same; second session:
(100.0 100.0) on both). The monitor's Timeline sorts a machine's own lines by the session clock, which starts at s 0 in
every session, and BUG-1061's `same_session` only keeps the lines of the stage and world the partner line names. Both
sessions here are Cell 9652F8AD, so the guest's own lines at s 10.010 (session 1) and s 10.013 (session 2) were both
candidates, and the one closest in session time won by some milliseconds: the host's first-session line at s 20.011 was
judged against the guest's second-session line at s 20.012 (pos (100 100), still in the join cinematic) and the host's
second-session line at s 10.003 against the guest's first-session one at s 10.010: 3.9 and 4.0 m, two lines in a row.
Whether it fails depends on the milliseconds and on where the first session's guest cell was: a flake of the monitor,
old (BUG-1061's matching, 0.7.14). `monitor.py replay` of the two game logs reproduces it.

## Fix
tools/tests/monitor.py: every record keeps its log line's wall clock; Timeline.window (with a partner record, used by
the drawn, ground, puppet_hp and hp rules) keeps only the subject's own lines whose session start (wall - s) is the
partner line's (within 5 s plus the distance in session time: written test logs have one wall time on every line).
When the subject has no line at that session start (logs of two machines with other clocks) the window stays as
before. Config `same_clock` (default 5.0 s; 0: the matching before). switch: none (test tool), the config is the twin.

## Verification
FAIL: lab 20261116-025154 sreport_leave (rel/0.7.15): `monitor.py replay` of its host and guest logs gives the drawn
violation with the old monitor (and with `same_clock=0`), none with the fix. gates_selftest twin `f_drawn_sessions`
(two Cell sessions of one world, the second's guest line closer by 15 ms: same_clock 0 fails, the default passes).
PASS: lab 20261116-085451 sreport_leave#1, #2 (fix/w51-t5, no monitor violation); gates_selftest f_drawn_sessions passes, f_drawn_sessions_old (same_clock 0) fails.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t5 (final-check triage of 0.7.15, group T5).
- 2026-11-18: released in 0.7.15.
