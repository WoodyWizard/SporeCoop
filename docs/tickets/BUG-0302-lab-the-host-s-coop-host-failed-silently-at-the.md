---
id: BUG-0302
title: lab: the host's coop.host failed silently at the start of a test (no hosting, the guest's join timed out) and twoinst waited 240 s before the test failed far away
status: fixed
kind: bug
stage: lab
reported: 2026-10-30
source: lab 20261030-074023 space_coop (lane 4)
role: 
versions: main 390f73d
fixed_in: 0.7.3.2
branch: fix/w11-space-join
commits: 1b8430e
tests: every twoinst test (start), space_coop
signature: host log "cheat console: coop.host <port> Alice" with nothing after it; guest "net: session closed (join timeout)"; "both games ready in ~267 s ... guest in game 263"
docs: docs/06-testing.md#testing-host-start-check
related: BUG-0301
---

## Report
Smoke run on main (lab 20261030-074023, lane 4): `space_coop` "FAIL the host has an NPC ship []",
its start took 267 s ("guest in game 263") instead of the usual 30 s.

## Root cause
Observed (logs): the host loaded the fixture and got `coop.host 47816 Alice` (SporeKit's console line),
but the co-op mod never hosted: no "hosting on UDP port" / session start lines, no trace lines of a
host, nothing else for 4 minutes. The guest's join to 127.0.0.1:47816 ended after 10 s with "session
closed (join timeout)" and the guest stayed in the galaxy menu. `twoinst.start` ignored both the
answer of `coop host` (its "host failed: ..." went only to the console reply, not to the game log) and
the result of `wait_guest_in` (240 s), so the test began without a guest; the NPC ship got no
squadron id (#-1) without a partner in the system and the first check failed. Not a gameplay bug.
Inferred (not proven, the console reply is lost): the bind of UDP 47816 failed. The lab's fixed co-op
ports 47810-47828 lie inside Linux's ephemeral port range (32768-60999), so any UDP socket of the
machine (another lane's guest, the unit tests of the same smoke run on lane 3, a dying Wine process of
the lane) can hold one for a moment.

## Fix
- coop.cpp `c_host`: a failed host is also written to the game log ("net: host failed on UDP port
  N: <error>").
- twoinst.py `host_up`: the answer of `coop host` is checked; a failure is printed and tried again
  (3 times, 3 s apart); a guest that is not in the host's world after `wait_guest_in` is printed with
  its status line, so such a start no longer fails far away without a word.

## Verification
- no-fail-run: a rare start flake (one start seen, in a smoke run of 14 tests), the console answer that would
  show the cause was not recorded; the change is diagnostics and a retry of the harness. Every start
  of lab 20261030-100310 (2 games) and of the campaign runs went through `host_up`.

## History
- 2026-10-30: opened (fix/w11-space-join) from lab 20261030-074023 space_coop; diagnostics and a
  retry added, cause inferred.
- 2026-11-01: released in 0.7.3.2.
