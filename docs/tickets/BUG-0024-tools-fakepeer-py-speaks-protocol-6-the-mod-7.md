---
id: BUG-0024
title: tools/fakepeer.py speaks protocol 6, the mod 7
status: fixed
kind: tooling
stage: All
reported: 2026-10-25
source: code review (tools comparison)
role: 
versions: 
fixed_in: 0.7.3.0
branch: fix/fakepeer-protocol
commits: ec57d4b, 5f6f12f, 70fa46d
tests: fakepeer_check
signature: 
docs: docs/06-testing.md
related:
---

## Report
Connection::looks_valid drops packets of another protocol version, so fakepeer is silently broken.

## Root cause
A hard-coded PROTO = 6. A run ending without WELCOME still printed `done` and returned success.

## Fix
Default to protocol 7, retain protocol 6 through `--protocol 6`, and test interoperability against
the C++ session. The planned automatic version lookup was replaced with explicit supported
versions and a test against `kProtoVersion`; check the wire format before adding a future version.

## Verification
`fakepeer_check.py` exercises the handshake, player state, chat, leave, build mismatch and a host
that never replies. Its silent-host check must fail if the no-WELCOME error path is removed.

## History
- 2026-10-25: reported (code review (tools comparison)).
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
- 2026-10-27: fixed on fix/fakepeer-protocol, not released yet.
- 2026-10-28: released in 0.7.3.0.
