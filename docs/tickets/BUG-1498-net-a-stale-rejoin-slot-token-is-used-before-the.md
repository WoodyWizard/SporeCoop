---
id: BUG-1498
title: Net: a stale rejoin slot token is used before the kept one after a crash (NET-2)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: guest
versions: 0.7.16.1 and earlier
fixed_in: 0.7.17
branch: fix/w80-net
commits: 0ff638f2
tests: tools/tests/coop_dna_own.py --case rejoin_failed (dna_own_rejoin_failed, twin dna_own_rejoin_failed_off), tests/space_tests.cpp (test_rejoin_slot_pair)
signature:
docs:
related: BUG-1135
switch: net.rejoin_slot_pair
---

## Report
External static audit of 0.7.15.5, item NET-2 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Inferred from the code (rejoin.cpp join_token): the slot file's token wins over the token a clean leave kept for this
host and name (`if (uint64_t t = read_slot()) return t;` before `kept`). The slot outlives a crash and a join that failed
at once: `do_join` writes the token (token_for_join) before `g_sess.join`, and a synchronous failure (an address that does
not parse, e.g. a port over 65535) returned without `rejoin::clear`. The next correct join to the real host then sent that
fresh token instead of the kept one: the host's per-player store (found by its key only) did not know the player (its own
DNA, kept Space state). (b) the same after a crash in a session with host A and a join to host B: B got A's token.

## Fix
rejoin_kept.h `SlotLine`: the slot holds `<token> <pair>`, the pair a hash of host address and name (KeptFile::key
under a fixed salt: no plain address on disk). rejoin.cpp `read_slot(pair)`: a slot of another pair is ignored, so the kept
token of this host wins again; a crash rejoin to the same host and name still sends the slot's token; a slot written by an
older version (the token alone) counts for any join as before. Switch `net.rejoin_slot_pair` (off: any join takes the
slot's token, 0.7.16.1).

## Verification
Open, waiting for the lab run (pending night run: dna_own_rejoin_failed, dna_own_rejoin_failed_off).
* Wanted PASS: `SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_rejoin_failed --lane N --retry 0 --no-build` (the guest spends 25, leaves, a join to 127.0.0.1:99999
  fails at once, the real join: the guest's DNA is the host's - 25, its own wallet).
* Wanted FAIL: `SPORE_DETECTOR=20 python3 tools/lab.py run dna_own_rejoin_failed_off --lane N --retry 0 --no-build` (SPORE_FIX_OFF=net.rejoin_slot_pair: the failed join's
  token is sent, the host sees a new player, the guest's DNA is the host's).
* Unit: tests/space_tests.cpp test_rejoin_slot_pair (pair match, another host, the switch off, an old slot, damaged slots).
0.7.17 release lab: PASS: dna_own_rejoin_failed (lab 20261124-020506); no-fail-run: verified by 0.7.17 release gate lab run.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fix and tests on fix/w80-net (0ff638f2); waiting for the night lab run.
- 2026-11-24: released in 0.7.17.
