---
id: BUG-0770
title: 0.7.12-only: session report and session settings state survive a voluntary leave, re-host or re-join
status: open
kind: desync
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
related: BUG-0767, BUG-0783
---

## Report
Found by reading the code (no player report). A guest that leaves the session with Leave (or joins / hosts another
session, or hot-reloads the mod) while the host's session report is asking it or while its report is being sent keeps
that state into the next session: the next session shows "<old host> (host) asks for your report", and a report that
was being uploaded goes on uploading to the NEW host (up to 25 MB of reliable pieces queued in front of the guest's own
messages). The host's session settings table also stays: in the next session the guest plays with the previous host's
values until the new host sends its table, and for the whole session when the new host is older than 0.7.12 (it never
sends one). A host that leaves while a round runs keeps the round: 120 s later a zip of a dead session is written, and
hosting again meanwhile answers "Already asking".

## Root cause
Inferred from the code (rel/0.7.12 7ec10ba3), confidence high:
* `sreport::on_disconnect()` and `settings::on_disconnect()` are called only from `g_sess.cb.disconnected`
  (mods/coop/coop.cpp:4221-4222). `net::Session::leave()` never calls `cb.disconnected`, so `do_leave()`
  (coop.cpp:4687), `end_open_session()` (coop.cpp:4648, a host / join while a session is open) and the unload path
  never reset them; `end_session()` calls neither module.
* sreport.cpp:456-501 `guest_tick()` runs whenever `is_guest()` is true again: in `GState::Sending` it sends the kept
  `g_out` with the old round id (the new host drops every piece: round mismatch) while
  `C.sess->pending_to(0) < window` (160 / 600 messages), so the guest's own reliable traffic (own creations, ready,
  colour request) waits behind it until the upload ends.
* settings.cpp:202-207 `raw()` returns `g_host.v[id]` while `is_client() && g_have_host && g_host_sent[id]`; both
  flags are only cleared in `on_disconnect()` (settings.cpp:192-200). Affected values: dna_pace (progress pace),
  npcs_target_partners, pack_rescue, pack_heal, joint_hunt, partner arrows / minimap.
* Host: `g_round` is never dropped at a session end; `tick()` times its guests out after `kAnswerWait` and writes.

## Fix
<!-- not fixed yet -->

## Fix idea
One reset on every session end: call `settings::on_disconnect()`, `sreport::on_disconnect()` and a new host-side
`sreport::on_session_end()` (drop `g_round` unless it is being written, clear `g_host_zip`) from `end_session()`
(block 1 or 3, every EndPath), keep the twin switch `core.session_report` / a new `settings.reset_on_leave` for the
old behaviour.

## Verification
<!-- not fixed yet -->

## How to test
New lab test `coop_sreport_leave.py` (2 instances): (a) the host asks, the guest answers Send, `coop.sreport test
big`-free normal report; the guest runs `coop.leave` while `coop.sreport status` says `guest=sending`, joins again;
assert `guest=` is not `sending` and the host's `coop.net` pending to the guest falls under 50 within 5 s.
(b) the host sets `coop.settings set all.dna_pace off`, the guest leaves and hosts alone: `coop.settings list`
on it must not say "the host's table" and dna_pace is its own value. FAILS on rel/0.7.12 7ec10ba3, PASSES with the fix.

## History
- 2026-11-10: opened.
