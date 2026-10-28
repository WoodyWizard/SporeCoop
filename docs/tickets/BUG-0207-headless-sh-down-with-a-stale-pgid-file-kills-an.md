---
id: BUG-0207
title: headless.sh down with a stale pgid file kills an unrelated process group of the user
status: fixed
kind: tooling
stage: All
reported: 2026-10-28
source: wave 4 tools review (fix/w4-tools)
role: 
versions: 
fixed_in: 0.7.3.0
branch: fix/w4-tools
commits:
tests: tools/tests/tools_selftest.py headless (make test)
signature:
docs:
related:
---

## Report
`tools/headless.sh` keeps the process group id of the headless GNOME Shell in
`$XDG_RUNTIME_DIR/sporecoop-headless/pgid`. `down` (and `up`, which calls `down` whenever it finds no
live Xwayland of the shell) checks only `kill -0 -<pgid>` and then sends SIGTERM and SIGKILL to the
whole group. After the shell died (a crash, a logout, a reboot without /run cleanup is not possible,
but a crash is), the number can belong to any other process group of the user: a terminal's job, an
editor, the owner's own Spore. The next `spore.sh run` (`headless.sh up`) then kills that group.

## Root cause
Found by reading (wave 4 tools review). The pgid file is trusted without checking that the group is
still the shell started from this runtime folder.

## Fix
`running()` accepts the group only when its leader still exists as a group leader and has
`XDG_RUNTIME_DIR=<our runtime folder>` in its environment (the leader is `dbus-run-session` started
with that variable; checked on the live shell of this machine).

## Verification
`tools/tests/tools_selftest.py headless` (in `make test`): a sleeping process in its own session,
its pid written into a fake runtime folder's pgid file, `headless.sh down` with that runtime folder.
FAIL with main's headless.sh (the process is killed), PASS with the fix.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w4-tools (found while checking the dev tools for BUG-0025).
- 2026-10-28: released in 0.7.3.0.
