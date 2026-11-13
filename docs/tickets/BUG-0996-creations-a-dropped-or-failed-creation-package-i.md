---
id: BUG-0996
title: Creations: a dropped or failed creation package is never closed or released (file handle and DatabasePackedFile kept for the process)
status: open
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-8
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0055, BUG-0370
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-8. Not seen at runtime, found by code reading. Every creation the mod applies
is written to a .package file under the co-op folder and registered with the game's resource manager. When a package
is dropped (a newer version of the creation, the session end's world packages) it is only unregistered; when Open or
RegisterDatabase fails the half-made object is returned as nullptr. In both cases the DatabasePackedFile (0x388 bytes)
stays allocated with its file open for the rest of the process. The code already works around the open file:
package_for writes `<name>_2.package` ... `<name>_49.package` when the old file is still in use, and fails the creation
("cannot write the package") after 48 tries.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/creations.cpp:1497-1520 register_package: operator new, ctor, AddRef ((db+4) vt[0]), Initialize (vt 0x04),
  Open (vt 0x18); on an Open failure (1510-1513) or a RegisterDatabase failure (1514-1517) it returns nullptr without a
  Close or Release (a failed RegisterDatabase also leaves the file open).
* creations.cpp:1522-1527 unregister_package: RegisterDatabase(false, db) only; drop_package (1615-1622) erases the map
  entry. Callers: 1861 (a world package replaced), 1884 (a player creation replaced), 3060 (session end).
* creations.cpp:1594-1603 package_for: the "_N" fallback when write_file fails because the old file is open.
Inferred (medium confidence): the game's Close is vt[7] and Release is (db+4) vt[1] (the audit's slots, consistent with
the AddRef at (db+4) vt[0]); whether the resource manager still holds resources read from a dropped package (so a
Release must wait) is not checked. Severity: low (a leak of a handle and ~1 KB per creation change; the 48-name limit
is reached only by one creation key changed 48 times in one game process).

## Fix idea
RE first: confirm Close (vt[7]) and Release ((db+4) vt[1]) in docs/re/creations.md and the .sym. Then close and release
on both failure paths of register_package, and on drop_package after the unregister (outside the frame, or deferred a
few frames if cached resources reference the package). Switch: `creations.package_close`.

## How to test
New tools/tests/coop_creations_package_close.py (creature fixture): the host changes its creature in the editor (or
sends a new version by the test command) 5 times; the guest's log/`coop.creations status` and the files: every older
`p0_*.package` is closed (a test command that tries to open it for writing, or the next version reuses the base name
instead of `_2`). FAIL on main (`_2`, `_3` names, the file locked), PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
