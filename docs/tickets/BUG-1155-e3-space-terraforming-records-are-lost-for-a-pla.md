---
id: BUG-1155
title: E3: Space: terraforming records are lost for a player whose planets of that star were not generated or who was not in Space, and after the 30 s send window
status: open
kind: desync
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Playability audit of 0.7.15 wave E (Space, item 4; out/audit-0.7.15/playability/space.md, not in git): K_TERRA_REC is
skipped when the receiver has not generated that star's planets or is not in Space (spacecoop.cpp, BUG-1042's
checks), and it is sent only for 30 s after the change, so terraforming can be lost for a player who arrives later,
and a guest's terraforming may not reach the host's record (nor its save) when the host had not generated that star.

## Root cause
Observed in the code (spacecoop.cpp apply_terra_rec, terra_rec_tick) and by the test with the switch off: the record
is dropped when `planet_record_checked` refuses it (outside Space, star +0xAC count 0 here), nothing keeps it, and the
sender stops sending 30 s after its last change. docs/re/space.md#space-terraform-late.

## Fix
spacecoop.cpp: a skipped K_TERRA_REC waits per PlanetID (at most 128) and is applied in the first Space second its
planet exists here; the host keeps the latest values of each planet it sent or received (at most 64) and sends a star
system's to a partner whose place enters that star, all of them to a partner it sees for the first time. Reset with
the Space world and the session. No wire change (K_TERRA_REC as before; a 0.7.14 peer applies or skips the resend as
it did). Switch `space.terra_rec_late` (off: 0.7.14). The ecosystem lists of BUG-1148 use the same pattern.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
