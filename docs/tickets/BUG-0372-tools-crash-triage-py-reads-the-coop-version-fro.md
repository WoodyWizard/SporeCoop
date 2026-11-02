---
id: BUG-0372
title: Tools: crash_triage.py reads the coop version from the crash file's summary of an earlier run
status: fixed
kind: tooling
stage: -
reported: 2026-11-01
source: player reports 2026-11-01 (host GOG 2024 + Steam guest, 0.7.3.3)
role: 
versions: 0.7.3.3
fixed_in: 0.7.3.4
branch: fix/w30-progress
commits: 600fd2d
tests: tools_selftest.py triage (g)
signature:
docs: docs/06-testing.md#testing-own-editor-locks
related: BUG-0247, BUG-0346
---

## Report
`tools/crash_triage.py` on the 2026-11-01 host crash printed coop 0.7.3.1, while the crash file says
`mods: coop 0.7.3.3`; it also printed the same fault twice (with and without "SporeApp.exe ").

## Root cause
Observed: the version was the first `coop X` of the text, which is the Summary's "(an earlier run)
latest co-op summary" of a 0.7.3.1 session; the log tail's line of the same fault had no module name,
so it was a second signature.

## Fix
The version comes from the Environment's `mods: coop X`, the coop section's `SporeCoop X`, the log's
load line, else the first `coop X` outside summary lines; faults are listed once per (code, address).

## Verification
FAIL: tools_selftest.py triage with SELFTEST_TRIAGE=<the tool of 617d579>: (g) fails (coop 0.7.3.1 and two signatures).
PASS: tools_selftest.py triage (g) ok, also in `make test`.

## History
- 2026-11-01: opened and fixed on fix/w30-progress.
- 2026-11-02: released in 0.7.3.4.
