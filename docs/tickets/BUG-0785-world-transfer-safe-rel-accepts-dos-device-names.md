---
id: BUG-0785
title: World transfer: safe_rel accepts DOS device names (con, nul, com1) as received file names
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: all
fixed_in: 0.7.13
branch: fix/w48-net
commits: 2f9c631b
tests: unit (make test: validate_tests test_world_path)
signature:
docs: docs/06-testing.md#testing-n1-net
related: BUG-0087, BUG-0242
switch: world.device_names
---

## Report
Found by reading the code. The check of a received world's file names refuses drive letters, streams, "..",
wildcards and trailing dots, but not the DOS device names: a host (modified) can send a file named "con", "nul",
"aux", "com1" or "lpt1.spo". On Windows CreateFileW on such a name opens the device instead of a file in the save
slot (writes go nowhere, or block on a serial port), and the world is then marked written without that file.

## Root cause
Kind bug, not security: tools/tickets.py keeps every open security ticket private, and this one is judged not exploitable against players in practice (see below); it is a hardening item.
Inferred from the code (main e49e1e23), confidence medium (the Windows behaviour per path part is documented;
not tried under Wine): mods/coop/world.cpp:69-90 `safe_rel()` checks each part for empty, trailing '.' or ' ',
depth > 4 and the characters `<>:"|?*` / control characters, but has no device-name check. The updater's pack check
has one (updater/pack.cpp:27 `device_name`, BUG-0242). Not exploitable against players beyond a failed world load,
so not private.

## Fix
Confirmed on the code (world.cpp safe_rel had no device check). `safe_rel` moved to mods/coop/world_path.h
(portable) and refuses every path part that is a Windows device name: con, prn, aux, nul, conin$, conout$, com0-9,
lpt0-9 and com / lpt with a superscript 1-3, any case, with any extension and with spaces before the extension.
"console.spo", "com10.txt" and the like stay allowed. Switch `coop.fix world.device_names off`.

## Fix idea
Reuse the updater's `device_name()` rule (con, prn, aux, nul, com0-9, lpt0-9, with any extension, case-insensitive)
in `safe_rel()` for every path part; switch `world.device_names`.

## Verification
* Unit (make test): validate_tests `test_world_path`: FAIL `validate_tests --nofix` (12 names accepted,
  out/n1-unit/validate_nofix.txt), PASS in make test twice (out/n1-unit/validate_pass_1.txt, validate_pass_2.txt).
* Not tried under Wine on a game (the device behaviour is Windows' documented path rule).

## How to test
Unit test (tests/validate_tests.cpp or net_tests): `safe_rel("con")`, `safe_rel("Planets/nul.spo")`,
`safe_rel("COM1.txt")` are false; `safe_rel("console.spo")` stays true. FAILS on main, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): device names refused; FAIL/PASS unit runs.
- 2026-11-12: released in 0.7.13.
