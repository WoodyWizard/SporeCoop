---
id: BUG-1152
title: E1: Space Rivals: the end of a session resets the mode, the guests' budgets and the relations between players
status: fixed
kind: bug
stage: Space
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-store
commits: f5e9e809
tests: space_session
signature:
docs: docs/01-architecture.md#arch-player-store
related: BUG-1149, BUG-1029, BUG-0192
switch: store.space_rivals
---

## Report
Playability audit (0.7.15, space.md row 21): the end of a session switched Rivals off and dropped the guests' budgets and the relations between players: the next evening began in Allies mode with the guests' money gone.

## Root cause
Observed in code: space_rivalry.cpp session_end: set_rivals(false), budget_clear, g_rel_names.clear.

## Fix
The host keeps the mode, the budgets (by stable key) and the relations (by key pair) across the session end and in the per-player store (world record `space.rivals`, per player `space.budget`); the next session of the world resumes Rivals mode (no split: everybody's money is its own), a guest's K_ASK gets its budget, the relations come back when both players are here. `coop.fix store.space_rivals off`: reset (0.7.14).

## Verification
space_session: FAIL (SPORE_FIX_OFF=store.space_rivals): out/lab/20261115-125647 (Allies mode, the guest's money
50000 not 57000, no war); PASS: out/lab/20261115-124902, out/lab/20261115-130851, out/lab/20261115-133047,
out/lab/20261115-134708.

* PASS post-merge of E3 / E4 (rel/0.7.15 at 94118b08): out/lab/20261115-141219, out/lab/20261115-141234 (space_session).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-store.
