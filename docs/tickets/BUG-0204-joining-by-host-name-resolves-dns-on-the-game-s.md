---
id: BUG-0204
title: joining by host name resolves DNS on the game's main thread: a slow name server freezes the game
status: fixed
kind: freeze
stage: Menu
reported: 2026-10-28
source: split from BUG-0138 (fix/w3-net)
role: guest
versions: 0.7.2.4 and earlier
fixed_in: 0.7.3.0
branch: fix/w4-installer
commits: cded566
tests: coop_join_name.py (suite join_name), net_tests test_join_by_name
signature:
docs: docs/06-testing.md#testing-installer-w4
related: BUG-0138
---

## Report
Third point of BUG-0138 (GitHub #117), not fixed there. A guest that types a host *name* (not an
IP address) and presses Join waits for `getaddrinfo` inside the click handler, on the game's main
thread: with a slow or unreachable name server the game stops drawing until the resolver gives up
(seconds, up to the resolver's own timeout).

How to reproduce: point the system at a name server that does not answer, type `somehost:47810`
into "Host address", press Join: the frames stop until "Cannot join: cannot resolve somehost".

## Root cause
`Session::join` -> `Endpoint::parse` (net/udp.cpp) calls `getaddrinfo` synchronously. Since
fix/w3-net a numeric address is parsed with `AI_NUMERICHOST` first and never waits; only names
still block. Inferred from the code, not run with a dead name server.

## Fix
Fixed on fix/w4-installer (cded566). `Session::join` parses the address without a lookup
(`Endpoint::parse_numeric`); a host name is looked up on a worker thread (`Endpoint::resolve_name`
behind `net::g_lookup`) and `join` returns at once. `Session::update` polls the lookup: on success it
sends HELLO (the 10 s join timeout starts then), on failure it leaves and calls
`cb.disconnected("cannot resolve <name>")`, which the window shows; a lookup that has not ended
after 20 s gives up. `leave()` drops the session's reference; the worker owns its own reference and
ends alone. In coop.dll the worker is started by `spawn_pinned` (coop.cpp): it holds a reference to
coop.dll and leaves through `FreeLibraryAndExitThread`, so a hot reload during a slow lookup does
not unmap its code (as BUG-0060). Switch: `coop.corefix async_resolve 0` (and
`net::g_fix.async_resolve`) brings back the lookup inside the click. Test helper: `coop.slowdns <ms>`
makes every lookup wait.

## Verification
Tests:

* `net_tests` `test_join_by_name` (make test, fake lookup of 1.5 s): `join("slowhost:port")` returns
  in 0 ms and connects; an unknown name ends with "cannot resolve nohost"; a leave during the lookup
  is clean. FAIL with `net_tests --old`: `join() returned after 1500 ms`.
* `coop_join_name.py` (two games, creature, lane 4), `coop.slowdns 4000`, join `localhost:<port>`:
  * FAIL `--without-fix`: `frames 1539 -> 1540 -> 1540; the join command took 4.0 s`, "0 frames in 2 s".
  * PASS: `frames 1294 -> 1327 -> 1458; the join command took 0.0 s`, the guest is in the host world,
    an unknown name ends with "cannot resolve".

Runs: FAIL without the fix: `out/w4-fail/join_name_old.log` (manual run on lane 4, worktree fix/w4-installer); PASS: lab run `out/lab/20261028-030533` (join_name; also the manual PASS out/w4-fail/join_name_fix.log), smoke `out/lab/20261028-030911` 14/14.

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w4-installer (wave 4): name lookup on a pinned worker thread, the join goes on in update(); unit and two-game tests.
- 2026-10-28: released in 0.7.3.0.
