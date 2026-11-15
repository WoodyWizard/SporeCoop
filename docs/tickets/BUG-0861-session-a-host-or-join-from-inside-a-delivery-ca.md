---
id: BUG-0861
title: Session: a host() or join() from inside a delivery callback can be torn down later by the deferred leave (suspected)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor 17844679, 956ee967
role: 
versions: 
fixed_in: 0.7.13
branch: fix/w48-net
commits: fa0b0a65
tests: unit (make test: net_tests test_restart_in_callback)
signature:
docs: docs/04-netcode.md#netcode-network-input, docs/06-testing.md#testing-n1-net
related: BUG-0029
switch: net.restart_gen
---

## Report
No report. Suspected: a callback that restarts the session (hosts again or joins) while a packet is being delivered
could get its new session torn down by the leave deferred earlier.

## Root cause
Inferred from code (confidence low): `mods/coop/net/session.cpp:171-179` defers `leave()` while `delivering_` is set
(`leave_pending_`), and `session.cpp:709-715` performs that pending leave after the delivery loop. If the callback, after
leaving, calls host() or join(), the deferred leave at the end of delivery may run against the new session. BUG-0029 made
this deferral.

## Fix idea
From the fork (jwhans0n/SporeCoopGA 17844679, 956ee967); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
They replaced `delivering_` / `leave_pending_` with a transport generation counter: leave, host and join bump it; the
delivery loop and the frame's flush capture the generation and stop processing the rest of the packet and the frame as
soon as it changed, so no stale deferred work touches the new session. They also share one packet budget across
mid-frame flushes. Their test: a fake peer whose WELCOME makes the callback re-host inside `cb.connected`.

## How to test
Portable net test (tests/net_tests.cpp style, two in-process sessions): the guest's connected callback calls leave() then
host(). FAIL: after the delivery the new host session is closed (role None); PASS: it stays hosted.

## Fix
Confirmed (unit FAIL run): host() / join() from the guest's cb.connected left the guest with role None after
the delivery: the deferred leave of the old session closed the new one.

`Session::restart_leave()`: inside a delivery the old session ends at once (BYE, transport closed), its peers are kept
in `zombies_` until Connection::on_packet returns (BUG-0029), a pending leave and its cb.disconnected of the old
session are dropped (logged). A generation counter `gen_` (host, join, every end of a session) stops the rest of the
packet, of the message handler (after cb.connected, cb.player_join, the token drop) and of the frame's update for the
old session. A plain leave() in a callback is deferred as before. Written on our code after the fork's idea
(generation counter); the fork's shared packet budget is not taken (not needed here). Switch `coop.fix
net.restart_gen off`. coop.cpp does not call host() / join() from a callback today: no game path known.

## Verification
* Unit (make test, also under ASan: net_tests_asan): net_tests `test_restart_in_callback` (leave + host, host,
  leave + join another host, inside the guest's cb.connected): FAIL `net_tests --off restart_gen` (role none in all
  three, out/n1-unit/net_off_restart_gen.txt), PASS in make test twice (out/n1-unit/net_pass_1.txt, net_pass_2.txt).

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): generation counter and zombies; FAIL/PASS unit runs.
- 2026-11-12: released in 0.7.13.
