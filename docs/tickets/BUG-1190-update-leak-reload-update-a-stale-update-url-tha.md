---
id: BUG-1190
title: update_leak, reload_update: a stale [update] url that update_check.py left in a lane's coop.ini wins over the test's own
status: fixed
kind: test
stage: Tools
reported: 2026-11-16
source: final check 20261116-025154
role: 
versions: 0.7.14.1
fixed_in: 0.7.15
branch: fix/w51-t1
commits: b92b4496
tests: update_leak, reload_update, update (tools/tests/update_check.py), harness_selftest (make test)
signature:
docs: docs/06-testing.md#testing-w51-t1-triage
related: BUG-0241, BUG-0060, BUG-0211
---

## Report
The 0.7.15 final check (lab 20261116-025154, lane 3): update_leak "FAIL the local server offers 99.0.0", "FAIL every
attempt stopped at \"not newer\" (0/5)" with `coop.update: update: idle running=0.7.14.1 latest= check=on
url=http://127.0.0.1:48002/latest`; reload_update "FAIL the new image logs only its own check (the old thread stayed in
the old image)" with `new image: update check: cannot reach 127.0.0.1 (error 12029)`. Both tests serve their own
"GitHub" on their own port (update_leak 47970 + lane, reload_update 47990 + lane = 47973 / 47993 on lane 3); the game
asked port 48002.

## Root cause
Observed: 48002 is update_check.py's port (47999 + lane) for lane 3. update_check.py (`update`, `update_api403`,
`update_flaky`) runs on a free lane outside the lab run and appends `[update] check=1 url=http://127.0.0.1:<47999+N>/...`
to that lane's coop.ini; it never removes it, and coop.ini survives the lab's deploys ("copied to SporebinEP1/mods on
first deploy"). After 2026-11-16 the lanes 2, 3, 4 and 5 all held such a section (48001, 48002, 48003 with the
/repos/... path of --api403, 48004): every earlier run that took them as the free lane left one. update_leak and
reload_update then append a second `[update]` section with their own url; the game reads the first one
(GetPrivateProfileString), so their checks went to the dead port 48002. Lane 9 had none: the tests pass there (lab
20261116-082959). Not a 0.7.15 change: a lane's history decides it (an old failure, first seen now because the final
check's lanes 2-5 had all served as the free lane by then).

## Fix
tools/tests/lane.py `ini_section(path, section, entries)`: every `[section]` block of the ini replaced by one (or only
removed with entries=None), the file's encoding and line ends kept; it returns the old bytes. update_leak and
reload_update set their `[update]` through it (and write the old bytes back as before); update_check.py sets its
section through it and removes it again when it ends (atexit), so no lane keeps a test's update url. The other tests'
checks are unchanged.

## Verification
* FAIL: lab 20261116-025154 (the final check, lane 3, its stale url 48002); lab 20261116-084124 (lane 9, the old tests,
  a stale `[update] url=http://127.0.0.1:48008/latest` planted in lane 9's coop.ini as update_check.py leaves it):
  update_leak "the local server offers 99.0.0" + "every attempt stopped at not newer (0/5)", reload_update "the new
  image logs only its own check".
* PASS with the same stale section planted: lab 20261116-085443 (update_leak `url=http://127.0.0.1:47979/latest`
  available 99.0.0; reload_update its own 47999) and lab 20261116-091534.
* update_check.py's cleanup: lane 9's coop.ini held no `[update]` after each of its runs (out/manual/
  update_check-lane9-1..3.log). Unit: harness_selftest `ini:` (replace, remove, UTF-16, a missing file).
* Lanes 2-5 still hold old sections (48001-48004) from runs before this fix: harmless for the fixed tests; a lane's
  owner can drop them with `lane.ini_section(<lane>/SporebinEP1/mods/coop.ini, 'update')`.

## History
- 2026-11-16: opened.
- 2026-11-16: fixed on fix/w51-t1.
- 2026-11-18: released in 0.7.15.
