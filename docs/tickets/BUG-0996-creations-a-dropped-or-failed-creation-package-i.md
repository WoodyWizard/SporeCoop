---
id: BUG-0996
title: Creations: a dropped or failed creation package is never closed or released (file handle and DatabasePackedFile kept for the process)
status: fixed
kind: bug
stage: All
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-8
role: 
versions: 
fixed_in: unreleased
branch: fix/w49-small
commits: f4aca716, 3e2de219
tests: creations_package_close
signature:
docs: docs/re/creations.md#creations-package-close, docs/06-testing.md#testing-t1-small
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
RE first (docs/re/creations.md#creations-package-close, GOG 2024): Close is the primary vtable slot 0x1C (0x8D9CD0),
GetRefCount 0x10, Release the refcount vtable's slot 1 ((db+4) vt[1], 0x69D3C0), which at 0 runs the deleting dtor
(its Shutdown closes the file). Confirmed at runtime: a guest switching between its own creatures A, B, A, ... made
the host write the second package of A as `_2` (the dropped package still open).

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
A dropped package is released 2 s later outside the frame (Close + Release) when GetRefCount is 1 and no table of
creations.cpp still names it; a package that failed to Open or to register is released at once. Switch
`coop.fix creations.package_close off` (0.7.13). The status has a `packages:` line (open, closing, closed, kept,
renamed).

## Verification
Test `creations_package_close` (coop_creations_package_close.py: the guest plays its own creatures A, B, A, B, A; the
host's packages).
FAIL (SPORE_FIX_OFF=creations.package_close): out/lab/20261113-031708. PASS: out/lab/20261113-031536, out/lab/20261113-033100-2 (renamed=0,
closed=4).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
