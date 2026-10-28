---
id: BUG-0069
title: netcode timeouts (5 s unstable, 15 s disconnect) do not match the code (30 s, no unstable state)
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#47
role: 
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w3-net
commits: 815813e
tests: docs_lint (make test)
signature:
docs: docs/04-netcode.md 4.6
related: #47
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/47 (full text, discussion).

docs/04-netcode.md section 4.6 says a connection is shown as "unstable" after 5 s without packets and dropped after 15 s. The code has no "unstable" state at all and drops a peer after 30 s (`Session::timeout_ms = 30000`). Anyone who tunes tests or reads a player's log by the doc expects the wrong timings (minor, documentation).

### Where
- The doc: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/docs/04-netcode.md#L92-L98
- The default timeout: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.h#L39
- Where it is used (guest side, then host side), plus the 10 s limits for joining and for unaccepted peers: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L555-L578
- `status()` (what `coop status` prints): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/net/session.cpp#L178-L199

### How to reproduce
Read section 4.6 next to `session.h:39` and `session.cpp:555-578`. In game: connect two instances, then freeze the guest (for example SIGSTOP its Wine process for 20 s). The host still lists the guest after 15 s, and drops it only after 30 s.

### Expected
Section 4.6 describes the real behaviour: "No packets for 30 s (timeout_ms; the game can block that long while saving or loading) -> disconnect; a guest that has no WELCOME after 10 s gives up; the host drops an unaccepted peer after 10 s. There is no 'unstable' state yet." Plus the real fields of `coop status`. Or, if a 5 s "unstable" warning is wanted, it becomes a feature ticket.

## Root cause
**What happens**

- `timeout_ms` defaults to 30000, with the comment "the game can block for a long time while saving/loading", and nothing in mods/coop changes it (`grep timeout_ms mods/coop` finds only session.h and session.cpp).
- No code path reports "connection unstable" (`grep -rn unstable mods/coop` finds nothing).
- The doc also says `coop status` shows "packet loss". `status()` prints `sent`, `recv`, `acked`, `resend` and `pending` counters, not a loss figure.
- What the doc gets right: the handshake gives up after 10 s on the guest, and an unaccepted peer is dropped by the host after 10 s.

### Confidence
CONFIRMED by reading the full path. The SIGSTOP check was not run.

## Fix
Docs only: docs/04-netcode.md 4.6, the two wrong bullets marked `~ superseded (2026-10-28)` and a
`CORRECTION (2026-10-28, fix/w3-net, BUG-0069)` with the real values: 30 s `timeout_ms`
(session.h:43, unchanged anywhere in mods/coop), 10 s join limit on the guest, 10 s for an
unaccepted peer on the host, no "unstable" state, and the fields `coop status` really prints
(`rtt`, `last`, `sent`, `recv`, `acked`, `resend`, `pending`). Checked against session.cpp
597-616 and `Session::status()` on 2026-10-28. A 5 s "unstable" warning would be a feature.

## Verification
Documentation; `tools/docs_lint.py` in `make test` passes. The SIGSTOP check was not run.

### Related
- docs/06-testing.md (BYE vs timeout behaviour in the leave tests).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #47.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w3-net (wave 3).
- 2026-10-28: released in 0.7.3.0.
