---
id: BUG-1281
title: A peer running the Lineages fork joins a session unrefused and its messages decode as ours
status: fixed
kind: bug
stage: All
reported: 2026-11-19
source: PR #196 review
role: 
versions: 0.7.15.2
fixed_in: 0.7.16
branch: fix/w56-general
commits: e364b36b
tests: unit (net_tests test_fork_refused)
signature:
docs: docs/04-netcode.md#netcode-fork-refused
related:
switch: net.refuse_fork
---

## Report
Found while reading PR #196 (the SporeCoop Lineages fork, based on 0.7.14.1): nothing in our handshake refuses a
peer that runs the fork, so a mixed session would start and decode the fork's messages as ours.

## Root cause
Inferred from the fork's source, not seen at runtime. The fork keeps kProtoVersion 7 and sends no family revisions,
so net/compat.h treats it as an older 0.7.x and lets it join with a warning. Its HELLO capability bit 8 is a str8
(CAP_CAMPAIGN_PROFILE) where ours is CAP_FAMILIES (u8 n, n x 5 bytes). Its PlayerState flags are u16 (its PF_DOWNED
128 is our PF_RESYNC) and its message ids 55, 58 and 70-94 mean other things or nothing here (ours: 70 HOST_BUSY,
71 TRIBE_ANIM, 72 CELL_MATE, 73 SPECIES_LINE).

## Fix
net::compat::is_fork (net/compat.h) knows a fork peer by either sign: HELLO / WELCOME caps bit 8 without a readable
family list (the fork's CAP_CAMPAIGN_PROFILE, a str8 "<version> / base parts"; ours always writes a family list with
the bit), or a version text with "lineages" in it (the fork sends "coop 0.7.14.1-lineages64-dev, SporeKit 0.3.17,
..."). Host (net/session.cpp, HELLO): the fork guest gets REJ_VERSION with "<host> plays SporeCoop 0.7.16; you have the
Lineages fork of SporeCoop (...): the two use different messages and cannot play together. Play with the same SporeCoop
as the host", before its rejoin token or anything else of the HELLO counts; the host's feed says "<name> (the Lineages
fork of SporeCoop, ...) was not let in" (cb.compat). Guest (WELCOME): a fork host's WELCOME makes the guest leave at
once with "rejected: <host> plays the Lineages fork of SporeCoop (...); you have SporeCoop 0.7.16: ..."; nothing after
the WELCOME is delivered (leave_pending_). Switch `coop.fix net.refuse_fork` (off: the fork joins as an older 0.7.x).
Test seams Session::test_hello / test_welcome send recorded bytes. Docs: docs/04-netcode.md#netcode-fork-refused.

## Verification
- Unit: net_tests --only test_fork_refused PASS (the fork's HELLO and WELCOME built field by field from its
  net/protocol.h Hello::write / Welcome::write, fork commit 93690d6a; no fork code run): both texts as above, the fork
  guest never in the roster, an ordinary guest still joins, a fork host's chat after its WELCOME never delivered.
- Unit: net_tests --only test_fork_refused --off refuse_fork FAIL (6 checks: the fork guest joined with no text; our
  guest stayed with the fork host).
- Not run against a real fork build (we do not run fork code).

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w56-general (WP-F1).
- 2026-11-23: released in 0.7.16.
