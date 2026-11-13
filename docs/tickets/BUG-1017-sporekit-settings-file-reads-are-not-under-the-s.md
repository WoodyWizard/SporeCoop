---
id: BUG-1017
title: SporeKit: settings file reads are not under the settings lock and log_once runs before it (inifile.cpp)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-8
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-kit2
commits: 702f51dd
tests: kit_initfail, kit_cmd_unload
signature:
docs:
related:
---

## Report
Audit 2026-11-12 for 0.7.14, finding KIT-8. Not seen at runtime, found by code reading. A settings value read while
another thread or another game instance converts the same ini file to UTF-16 (make_unicode: a temp file and
MoveFileExW over the original) may come back as the default once; two threads failing a non-ASCII write at the same
moment race on the static `told` set of log_once.

## Root cause
Observed in code (main efd2e0ba), confidence high for the code, medium for the effect:
* sporekit/inifile.cpp:211 write_str calls log_once before it takes g_mu (:214); log_once (:54) inserts into a
  function static std::set without a lock. Every other log_once call is under g_mu (make_unicode, :125). A race only
  when two threads write a non-ASCII value that cannot be stored in Unicode at the same time (rare).
* :191 read_str calls make_unicode (under g_mu and the file lock) but then GetPrivateProfileStringW without either;
  :200 read_int takes no lock at all. A conversion by another thread or instance between them replaces the file with
  MoveFileExW (:182). Inferred: the read sees the old or the new file (rename is atomic), but on Windows the open can
  fail with a sharing violation during the replace, so the default is returned once.
Player path: settings are read at start and when the co-op window opens; two game instances on one PC (tests, or a
player running two copies) or the setup writing coop.ini while the game reads it. Severity: Low.

Observed vs inferred (2026-11-13, fix/w49-kit2): the missing locks observed in code; a default value returned during a replace was never seen (Windows sharing semantics inferred).

## Fix idea
Take g_mu (and FileLock with a short wait) around the profile reads; move log_once under g_mu in write_str. Switch:
the existing skini::g_lock_fix covers it, or a new `kit.fix ini_read_lock`.

## How to test
New unit-style kit test (kit_ini_race.py, one instance): a console command converts the file in a loop
(`kit.test.ini convert`) while another reads a key 10000 times; count reads that return the default. FAIL on main if
any default is seen under Wine (may not reproduce: Wine's rename semantics differ), PASS with the lock.

## Fix
sporekit/inifile.cpp: read_str (after its make_unicode) and read_int take g_mu and the file's cross-process lock
(FileLock, at most 500 ms; the read goes on without it after that) around GetPrivateProfileStringW / IntW, the same
locks as the conversion and the writes. write_str takes g_mu before its log_once (the told set is guarded by g_mu
everywhere now). Switch: the existing `kit.fix inilock 0` (skini::g_lock_fix) also drops the read locks.

## Verification
no-fail-run: race with another thread's or instance's MoveFileExW over the file; Wine's rename does not give the
sharing violation Windows can, so no default could be provoked; fixed by inspection. Regression: every lab run reads
the settings at start (kit_cmd_unload out/lab/20261113-083520, out/lab/20261113-083551, kit_initfail out/lab/20261113-083655, out/lab/20261113-083723, out/lab/20261113-083757 (notify, hotreload, reload_update, kit_cmd_fault) passed).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-kit2 (no-fail-run, decision 4).
