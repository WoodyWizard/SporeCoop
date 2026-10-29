---
id: BUG-0226
title: spice sent with the co-op spice gift is lost when it comes back while the sender is not in Space
status: fixed
kind: bug
stage: Space
reported: 2026-10-28
source: GitHub #160 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-space
commits: 8d9ce51
tests: space_gift_refund (coop_space_gift_refund.py)
signature:
docs: docs/re/space.md#space-object-serial
related: BUG-0058, BUG-0225
---

## Report
From GitHub issue #160: https://github.com/WoodyWizard/SporeCoop/issues/160 (the full analysis, links and the scenario are there).

The co-op spice gift (the co-op window's Give button in Space gives 10 spice; `coop.space give <n>` in tests; spacecoop.cpp K_GIFT / K_GIFT_BACK) takes the spice from the sender's cargo first. If the receiver has no room, the spice comes back, but when the sender is outside Space at that moment (galaxy menu, an editor, a load) `add_spice` fails and the spice is dropped without a notice. BUG-0058 (fd814d3, release 0.7.3.0) fixed this pattern for the trade feature (space_trade.cpp) only. Severity LOW, data loss of a few spice units; not a regression (the code is the same as in 940fc74).

## Root cause
Observed: K_GIFT_BACK handled outside Space called `add_spice`, which failed without a cargo hold, and the spice was dropped silently (guest 15 of 20 spice, lab 20261028-225557 with the fix off).

## Fix
spacecoop keeps the unanswered gifts (`g_gifts_out`, 16 at most, cleared at session end). A K_GIFT_BACK is matched once (receiver, key, count) and erased before anything is paid; in Space it is added at once, else owed (`g_gifts_owed`, memory only) and paid on the first Space frame of the same Space world (`spaceext::world_epoch`), dropped with a log line in another world. `add_spice` refuses outside Space; the count is checked 1..999. Switch `space.gift_refund`. Test hooks: `coop.space gift hold|noroom 0|1` (receiver), `coop.space away 0|1` (shared). Simpler than the issue's design: the queue stays in spacecoop (not a shared `space_owed` module) and the pay rule is "same Space world" only; the finer rule (same game file saved since) belongs to BUG-0225's open trade part.

## Verification
PASS out/lab/20261028-224835 (20 -> 15 -> 20, still 20 10 s later); FAIL with `SPORE_FIX_OFF=space.gift_refund` out/lab/20261028-225557 (15 of 20).

## History
- 2026-10-28: opened.
- 2026-10-28: fixed on fix/w5-space (8d9ce51): owed until back in Space; test space_gift_refund.
