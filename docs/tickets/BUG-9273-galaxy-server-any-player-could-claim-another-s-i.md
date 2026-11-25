---
id: BUG-9273
title: Galaxy server: any player could claim another's identity; any address could get a slot and answers
status: fixed
kind: security
stage: galaxy
reported: 2026-11-19
source: alpha review 2026-11-18 S2, S3 (decision 6S A, owner's continue rule)
role: server
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: c3187171
tests: tools/tests/galaxy_server_check.py (18 checks; 3 new); manual on TITAN QA 8 + 9
signature: "refused <address>: identity <id> with another key"
docs: docs/12-server-and-players.md#server-security-pass
related: BUG-9256
---

## Report
2026-11-18 review: S2, the identity in HELLO was self-declared and sent to everyone: a client that said another
player's identity took that player's party seat and removed them from it; S3, no proof that a HELLO's address was
real (a spoofed address got a slot and the server's answers).

## Root cause
✔ HELLO: u32 proto, u64 identity, names; the server believed both.

## Fix
Protocol 6:
* S2: each profile makes a secret key once (identity::galaxy_key, galaxy_key.bin beside identity.bin) and sends it in
  HELLO; the server keeps the first key of each identity (<worlds>/identities.txt) and refuses that identity with any
  other key (kIdentityTaken). The key only goes to the galaxy server, never to other players.
* S3: a HELLO from an address without a slot must carry the server's cookie for that address (a keyed hash of the
  address and the minute, a secret new each run): without it the server answers a CHALLENGE (8 bytes, smaller than
  the HELLO) and makes no slot; the game sends HELLO again with it.
* Left (decision, gameplay): S6, who may host and store over a planet.

## Verification
no-fail-run: protocol checks. tools/tests/galaxy_server_check.py: 18 of 18 pass (three new: a HELLO without its
cookie gets a CHALLENGE and no slot; a wrong cookie gets no slot; Host's identity with another key is refused); the
other 15 unchanged. In game (TITAN): QA 8 and QA 9 connected through the challenge, QA 8 hosted Samuth, QA 9 joined
(2 players); identities.txt holds their 2 keys.

## History
- 2026-11-19: opened and fixed.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
