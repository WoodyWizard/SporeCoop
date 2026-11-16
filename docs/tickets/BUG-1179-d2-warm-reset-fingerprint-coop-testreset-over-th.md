---
id: BUG-1179
title: D2: warm-reset fingerprint: coop.testreset over the lifecycle registry and the profile files; twoinst falls back to a cold start on a difference
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labstate
commits: eddafb3a, ede74ddb
tests: warm_fingerprint (coop_warm_fingerprint.py), npc_tests test_fingerprint, lab_selftest game_log_case (fingerprint)
signature:
docs: docs/06-testing.md#testing-warm-fingerprint, docs/01-architecture.md#arch-lifecycle-fingerprint
related: BUG-1055, BUG-0430, BUG-1097, BUG-0898, BUG-1176, BUG-1177
---

## Report
D2 of docs/plan-0.7.15.md (audit docs/audit-0.7.15/tests.md TST-4): warm reuse leaks state between tests (BUG-0257,
0312, 0356, 0396, 0437, 0601, 0898, 0975, 1097, 1099; 2 of the 13 failures of the 0.7.14 final check), and twoinst
keeps a growing hand list of what to reset. Wanted: a state fingerprint of the games (over the lifecycle registry of
BUG-1055) and of the profile files the tests depend on, compared after a warm reset with a cold start's; a cold start
on a mismatch, naming the entries.

## Root cause
Tooling: the warm reset reset what twoinst knew of (forget_*, KNOBS, clear_faults, coop.uistate reset) and nothing
checked that the games were where a cold start leaves them.

## Fix
* mods/coop/lifecycle.h: every entry has a digest (`LIFECYCLE_STATE_D(..., digest)`, else its held() count);
  `lifecycle::fingerprint()` (the entries with a digest, fnv-1a hash), `lifecycle::reset_all(on)`.
* mods/coop/coop_cmds.cpp `coop.testreset [fp | plant <n>]`: reset_all (both scopes, mod state only, no hooks, no
  game calls) and the fingerprint (`testreset: reset=1 fix=1 entries=N holding=M hash=H`, one line per entry still
  holding something); `fp` only prints; `plant` (tests) sets `test.planted_leftover`, an entry whose reset forgets it.
  No behaviour change for players (a console command; nothing calls it outside the lab).
* tools/tests/twoinst.py: the cold start records the fingerprint (each game in its galaxy menu; the files
  `SporeCoop/choices.ini`, `SporeCoop/worlds.ini` (their values), `EditorSaves.package` (sha1) of both profiles right
  after its clean-up); warm_reset takes it after the leave and returns a refusal on a difference ("(no warm reuse: the
  state after the reset differs from the cold start: guest test.planted_leftover cold - now 00000007; cold start)"),
  _start then starts cold. `SPORE_WARM_FINGERPRINT_FIX=0`: no comparison (the twin; a TWIN_ENV name, out of the flaky
  ledger). `twoinst.LAST_WARM` tells a test what its start did.
* Test `warm_fingerprint` (suite, warm, creature): a clean warm start stays warm; a planted leftover and a worlds.ini
  record (BUG-1176) refuse it naming the entry; the cold start clears both.

## Verification
* FAIL (twin `SPORE_WARM_FINGERPRINT_FIX=0`): out/lab/20261115-202550 (lane 9: the start after the planted leftover
  stays warm, the guest still holds `test.planted_leftover 7`; the worlds.ini record stays too).
* PASS: out/lab/20261115-202307 (lane 9: start 2 warm, fingerprint 0.09 s; start 3 refused "guest
  test.planted_leftover cold - now 00000007" and cold; start 4 refused "guest SporeCoop/worlds.ini cold none now
  cfeab14d5803" and cold).
* Unit: tests/npc_tests.cpp test_fingerprint; lab_selftest game_log_case (the parse, a named leftover, a named file,
  an older DLL not compared, an empty ini = no ini); `make test` passed.
* Second PASS: out/lab/20261115-205018 (lane 9).
* Normal warm resets stay warm, cost on the warm path: out/lab/20261115-205013 (lane 10, smoke members team, select,
  frames_loop, puppet_jump PASS): select warm (warm reset 17 s, fingerprint 0.02 s, "same as the cold start"),
  frames_loop warm (23 s, fingerprint 0.03 s), puppet_jump cold only for the earlier knob `frames.source loop` (BUG-0430,
  as before). In all runs the fingerprint took 0.02-0.09 s of the warm reset (two console calls and three files per
  profile), far under the 1 s bound.

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labstate (coop.testreset, the fingerprint in twoinst, warm_fingerprint).
