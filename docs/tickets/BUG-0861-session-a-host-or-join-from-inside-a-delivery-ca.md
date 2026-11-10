---
id: BUG-0861
title: Session: a host() or join() from inside a delivery callback can be torn down later by the deferred leave (suspected)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor 17844679, 956ee967
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0029
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
## Verification

## History
- 2026-11-10: opened.
