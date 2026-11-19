---
id: BUG-1281
title: A peer running the Lineages fork joins a session unrefused and its messages decode as ours
status: open
kind: bug
stage: All
reported: 2026-11-19
source: PR #196 review
role: 
versions: 0.7.15.2
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
Proposed: detect a fork peer (its version string, or bit 8 carrying a payload that is not a family list) and refuse
it with a clear text in both roles, before any message is read; a unit test with a recorded fork HELLO. Needed before
any port from the fork that takes new message ids.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
