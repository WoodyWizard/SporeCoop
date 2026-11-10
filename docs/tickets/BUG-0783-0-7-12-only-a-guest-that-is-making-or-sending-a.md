---
id: BUG-0783
title: 0.7.12-only: a guest that is making or sending a report ignores the next Ask; its host waits the full 120 s
status: open
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0767, BUG-0770
---

## Report
Found by reading the code. A guest that is still making or sending its report for one round ignores a new Ask: the
host's new round lists that guest as "asked" and waits the full 120 s before it writes the zip. With BUG-0770 (the
state survives a leave) this also happens with a guest that left another session in the middle of sending: it
ignores the first real Ask of its new host.

## Root cause
Inferred from the code (rel/0.7.12 7ec10ba3), confidence medium (the first case is by design and logged):
mods/coop/sreport.cpp:429-432 `guest_message(kAsk)`: `if (g_gs == GState::Making || g_gs == GState::Sending) {
LOG(...asked again while sending...); return; }`: no answer goes back, so the host's `Round::tick` waits
`kAnswerWait` (120 s).

## Fix
<!-- not fixed yet -->

## Fix idea
Answer a new Ask at once while busy (kAnswer with send = 0 and a "busy" detail, or a new kBusy kind the host shows as
"busy"), and drop a stale Sending state on the new session (BUG-0770).

## Verification
<!-- not fixed yet -->

## How to test
Lab (2 instances): the host asks, the guest answers Send; while `coop.sreport status` on the guest says sending, the
host asks again (after the first round is written); assert the second round finishes within 10 s with the guest
listed. FAILS on rel/0.7.12, PASSES with the fix.

## History
- 2026-11-10: opened.
