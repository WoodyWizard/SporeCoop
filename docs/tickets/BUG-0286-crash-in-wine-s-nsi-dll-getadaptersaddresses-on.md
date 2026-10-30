---
id: BUG-0286
title: Crash in Wine's nsi.dll (GetAdaptersAddresses) on the main thread after a guest leaves in Space
status: fixed
kind: crash
stage: Space/Any
reported: 2026-10-29
source: lab
role: guest
versions: 0.7.3
fixed_in: 0.7.3.1
branch: fix/w8-adapters
commits: 042a194 6a6bb7d 499c8d2 (tests 56f498a 3b53d69, docs 0d70eaf 4736828)
tests: tools/tests/coop_adapters.py (suite adapters)
signature: 0xC0000005 ucrtbase.dll+0x14A9C (memcpy) < nsi.dll+0x191B < nsi.dll+0x150A < iphlpapi.dll < coop.dll local_addresses (GetAdaptersAddresses), main thread, from the co-op window refresh (sk::h_WndProc / run_outside_frame)
docs: docs/04-netcode.md#netcode-wine-nsi, docs/06-testing.md#testing-adapters, docs/known-issues.md#issue-wine-nsi-other-callers
related:
---

## Report
Final 4-player campaign (lab, Wine 10.0, GOG 2024, 4 games in one bottle): in Space the test made a
guest leave ("net: session closed (user)"), the session summary was logged and 1.4 s later the
guest crashed: 0xC0000005 in ucrtbase.dll+0x14A9C (memcpy), read of 0xD54CF6CC on the main thread,
chain nsi.dll+0x191B < nsi.dll+0x150A < iphlpapi.dll+0xE962 < iphlpapi.dll+0x5238 <
coop.dll local_addresses() (GetAdaptersAddresses) < SporeKit run_outside_frame < sk::h_WndProc <
App::Canvas::Tick. The watchdog "no frames for 10 s" finding came 3 min after the crash (the crash
handler's full report never finished): a consequence, not a cause.

## Root cause
* Observed: Wine 10.0's `NsiEnumerateObjectsAllParametersEx` (dlls/nsi/nsi.c) takes the entry
  count from the \\.\Nsi reply and copies count * size bytes without checking it against the count
  it asked for. In the crash the stack shows memcpy(dst 0x25CF5EA0, src 0x0C5CF58C = out+4,
  n 0xC8F00180): a garbage count. ucrt copies backwards when dst > src, so it faulted on its first
  read (nothing written). When it copies forwards it overwrites the heap first (later heap faults).
* Observed: two games of one Wine prefix calling GetAdaptersAddresses in a loop at the same time
  crash one of them within seconds, with the player's exact chain (lab run 20261029-200838:
  nsi.dll+0x191B < nsi.dll+0x150A < iphlpapi < coop.dll). A lookup in each game 4 ms after start-up
  (both games starting together) faulted the same way in 3 of 3 cold starts.
* Observed (negative): one game, two threads looking up together (AF_INET / AF_UNSPEC
  GetAdaptersAddresses, GetAdaptersInfo, gethostbyname), 1500 times: no fault; 1000 serial lookups
  in one game: no fault. A per-prefix named mutex around every lookup did not prevent it either
  (lab runs 20261029-194630, 20261029-195441: a game lost each time).
* Inferred: the reply of Wine's shared nsiproxy.sys (one winedevice.exe per prefix) is garbage when
  several processes of the prefix use it; the campaign had 4 games in one bottle whose co-op
  windows looked up every 10 s. Why exactly is not analysed. Players with one game per prefix are
  rarely exposed, but any other Wine process using the device can trigger it.

## Fix
`mods/coop/adapters.cpp`, switch `core.adapters` (`coop.fix core.adapters off` = the old
synchronous lookup on the caller's thread every 10 s):
* the lookup runs on a worker thread, never on the main thread, never at start, at most every
  5 min when the co-op window or the LAN scan asks; the main thread reads the last list;
* under Wine it never calls iphlpapi: the source address of UDP sockets connected to 8.8.8.8 /
  192.168.255.254 / 10.255.255.254 (nothing is sent) and getaddrinfo(gethostname()); a private
  address gets a /24 subnet broadcast. On Windows GetAdaptersAddresses with the documented
  ERROR_BUFFER_OVERFLOW retry, one at a time per session (named mutex);
* a fault guard: a vectored handler sends a faulting worker to a landing function (no __try on
  i686 mingw/clang), which releases the mutex, logs the fault and ends the thread; the list keeps
  its last value and no lookup runs again in that game;
* a running LAN scan takes the addresses of a later lookup (`adapters::generation()`);
* `coop.adapters [stress N|fault]` for tests.

## Verification
Lanes 3 and 9, GOG 2024, Wine 10.0, `tools/tests/coop_adapters.py` (two games of one bottle look up
900 times each at the same moment; then the guest leaves, the next lookup faults on purpose and the
co-op window opens):
* FAIL with `SPORE_FIX_OFF=core.adapters`: out/lab/20261029-193811 (lane 3: a game crashed in the
  stress part, heap fault in ntdll), out/lab/20261029-200838 (lane 9: the player's exact chain
  nsi.dll+0x191B < nsi.dll+0x150A < iphlpapi < coop.dll).
* PASS with the fix: out/lab/20261029-200625, out/lab/20261029-200736 (lane 3: 900 socket lookups
  each, no fault; the injected fault contained, frames go on, no crash report).
* An earlier version (mutex only, no socket path) FAILED with the fix on: out/lab/20261029-194630,
  out/lab/20261029-195441; a version that looked up at start broke ui_status (creature fixture,
  out/lab/20261029-192709): both dropped.
* Related: ui_status, leave_cleanup, fix_registry (out/lab/20261029-201826, lanes 3 and 9); `make test` passes.

## History
- 2026-10-29: opened.
- 2026-10-29: fix/w8-adapters: reproduced with two games of one bottle, fixed behind core.adapters.
- 2026-10-29: released in 0.7.3.1.
