---
id: BUG-1497
title: Net: a pointer into a temporary vector of players (a peer's version read after the vector died) (NET-1)
status: fixed
kind: bug
stage: All
reported: 2026-11-23
source: audit
role: 
versions: 0.7.16.1 and earlier
fixed_in: unreleased
branch: fix/w80-net
commits: 016fef16
tests: make test (build); regression: evolve_inplace tests of BUG-0508
signature:
docs:
related: 
switch: none (undefined behaviour on the old side: a read of freed heap, not observable)
---

## Report
External static audit of 0.7.15.5, item NET-1 (out/research/ext-audit-0.7.15.5.md); verified REAL in out/research/ext-audit-verify-net-core.md (the row names the lines, the failure and the fix size; static reading, no lab). Not taken by 0.7.16.1; planned for 0.7.17: docs/plan-0.7.17.md item 11, WP-N1 (fix/w80-net). A behaviour change gets a switch and a twin.

## Root cause
Observed in the code (session_glue.cpp peer_version, rel/0.7.17): `for (auto& p : g_sess.players()) if (p.id == id)
return &p.version;` with `players()` returning `std::vector<PlayerInfo>` by value: the pointer named a string inside the
temporary vector, which died when the function returned; `evolve_inplace_ok` then read `v->empty()` from freed heap. Reached
on every host evolution with a guest (after the BUG-0508 wait and in the in-place notice). Practical harm is about nil (the
free path overwrites only the first 8 user bytes, `version` sits at offset 16), as the verify table says.

## Fix
session_glue.cpp: `peer_has_version(id)` answers the question itself (a bool from inside the loop); no pointer leaves the
temporary. No switch: the old side is undefined behaviour with no observable effect to test (R4 / R6 spirit).

## Verification
* no-fail-run: undefined behaviour without an observable failure (freed heap read in the same few instructions); the
  change is a local refactor checked by the build and make test.

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 11 (WP-N1), branch fix/w80-net.
- 2026-11-23: fixed on fix/w80-net (016fef16); closed with no-fail-run.
