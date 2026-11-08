---
id: BUG-0691
title: Shared money / food: a guest's credit that reaches the host while the host cannot read the resource (load, menu) is never counted or acked
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0423 (0.7.11)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b
commits: f1d45292
tests: res_away, res_away_tribe
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0423
---

## Report
From the K2a audit of BUG-0423 (no player report). shared.cpp on_message returns before the ack when the host's game cannot read the resource (`if (!r.get(cur)) return;`): the host in a load, the galaxy menu or without its player tribe / civilization. A guest's earned Civ money or Tribe food sent then (MSG_RES_ADD) is never counted, never acked: the guest keeps it in flight and shows it, the host never has it; both games' totals differ from then on.

## Root cause
Observed (lab): shared.cpp on_message returned before the ack (`if (!r.get(cur)) return;`) when the host's game could not read the resource; the guest kept the change in flight, the host never applied it (host 1000, guest 1300).

## Fix
`res.owed`: the host always acks MSG_RES_ADD and keeps the change as owed, paid into its game in the first tick the resource is readable in its mode (shared.cpp).

## Verification
* FAIL with `SPORE_FIX_OFF=res.owed`: out/lab/20261107-222350 (Civ money), out/lab/20261107-225459 (Tribe food).
* PASS: out/lab/20261107-221942, out/lab/20261107-222455, out/lab/20261107-224108.

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state-b): fixed.
