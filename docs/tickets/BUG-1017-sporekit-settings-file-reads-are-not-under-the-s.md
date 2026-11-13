---
id: BUG-1017
title: SporeKit: settings file reads are not under the settings lock and log_once runs before it (inifile.cpp)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) KIT-8
role: 
versions: 
fixed_in:
branch:
commits:
tests:
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

## Fix idea
Take g_mu (and FileLock with a short wait) around the profile reads; move log_once under g_mu in write_str. Switch:
the existing skini::g_lock_fix covers it, or a new `kit.fix ini_read_lock`.

## How to test
New unit-style kit test (kit_ini_race.py, one instance): a console command converts the file in a loop
(`kit.test.ini convert`) while another reads a key 10000 times; count reads that return the default. FAIL on main if
any default is seen under Wine (may not reproduce: Wine's rename semantics differ), PASS with the lock.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
