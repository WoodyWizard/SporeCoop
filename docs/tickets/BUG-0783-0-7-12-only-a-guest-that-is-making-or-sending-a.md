---
id: BUG-0783
title: 0.7.12-only: a guest that is making or sending a report ignores the next Ask; its host waits the full 120 s
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: 
versions: 0.7.12 (rel/0.7.12, unreleased)
fixed_in: unreleased
branch: fix/w47-b2
commits: dbb4498b
tests: sreport_leave
signature:
docs: docs/REPORTING.md#reporting-session
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
`guest_message(kAsk)` (sreport.cpp): a guest that is Making or Sending and gets an Ask of another round takes it: the
report being made is answered to the new round when made; an upload is answered (`kAnswer` send=1, its size) at once
and starts again from offset 0 under the new round id (the pieces of the old round still queued are dropped by the
host: round mismatch). The host asks again only after its previous round ended (written or timed out), so the old
transfer is dead, and the player already chose Send in this session. A second Ask of the same round is still ignored.
Twin switch `coop.fix core.sreport_reask off`. Test knobs: `coop.sreport test stall` (the upload of the first round
stops halfway), `coop.sreport stallwait <s>` (the host's `Round::stall_wait`, default kStallWait 60 s).

## Fix idea
Answer a new Ask at once while busy (kAnswer with send = 0 and a "busy" detail, or a new kBusy kind the host shows as
"busy"), and drop a stale Sending state on the new session (BUG-0770).

## Verification
`coop_sreport_leave.py` part A (the guest's upload hangs, the host times it out after 5 s and asks again):
* FAIL with `SPORE_FIX_OFF=core.sreport_reask`: lab 20261110-183843 (25 s after the second Ask still "Reports: 0 of
  1 - Bob: asked"; the guest says sending).
* PASS: lab 20261110-183103 (the second round written 1 s after the Ask, "Bob: sent (1299 KB)"), 20261110-184220 (two runs, every part).

## How to test
Lab (2 instances): the host asks, the guest answers Send; while `coop.sreport status` on the guest says sending, the
host asks again (after the first round is written); assert the second round finishes within 10 s with the guest
listed. FAILS on rel/0.7.12, PASSES with the fix.

## History
- 2026-11-10: opened.
- 2026-11-10: fixed on fix/w47-b2 (a busy guest takes the new round; test sreport_leave part A).
