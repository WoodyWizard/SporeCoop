---
id: BUG-0423
title: class: credits and effects dropped or undone (ledger, ack, owed effects) (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state-b, fix/w46-k2a-state
commits: f1d45292 1391b0ac
tests: res_away, res_away_tribe, civ_away_buy, space_reward_away, space_contract_clock, res_cause
signature:
docs: docs/re/host-centric.md#host-centric-away-credits
related: BUG-0691, BUG-0692, BUG-0693, BUG-0694, BUG-0695, BUG-0696
---

## Report
Fixed per case: BUG-0058, 0220 (join path only), 0225, 0226, 0228, 0233, 0239.
Siblings not covered (obs): shared.cpp MSG_RES_ADD returns before the ack when the host cannot read the value (:281 vs :326): guest-earned Civ money / Tribe food lost while the host is in the menu; have=false on a mode mismatch (:216-218, :294): a guest's spend right after an editor or load is undone (free money/food); Civ guest purchases, planner buildings, superweapons, gifts silently dropped while the host is away (civ.cpp:402-421, 455-477, 802-813; send_cmd civ.cpp:331, tribe.cpp:303); Space contract / K_MISSION tool rewards lost outside Space (space_contracts.cpp:243/523-545, spacecoop.cpp:1164-1190); contract deadlines on wall-clock time; MSG_RES (money, spice) has no cause tail (BUG-0389 class for Civ/Space).
General fix: ledgers pause, never drop; always ack; one owed-effect queue keyed by world identity; host-absent check in every command sender with feedback. Outcome check: every credit path with the receiver in menu / editor / load, game values equal on both machines.
Source: docs/generalisation-plan.md G6.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2a audit (0.7.11): the ledger's host side returned before its ack when the resource was unreadable, the guest's sync state was dropped on a mode switch, Civ / Tribe commands and Space rewards were dropped while the receiver was away, contract deadlines ran on the wall clock, the shared total had no cause.

## Fix
Shared methods: always ack plus an owed accumulator paid when the game can hold it (res.owed); the sync state kept over a mode switch (res.mode_keep); one held-command queue for the host-away case (net/held_cmds.h, civ.cmd_hold / tribe.cmd_hold, forgotten for a leaver through player_life); owed Space rewards (space.reward_owed); the contract clock on time in Space (space.contract_clock); a cause tail on MSG_RES (res.cause). Children BUG-0691..0696 all fixed with twin runs (their tickets; verified by reading the run dirs of fix/w46-k2a-state-b, and the tests passed again on the merged build out/lab/20261107-231435-2).

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2a final runs on the merged branch: detect_* of all five stages out/lab/20261107-231824, play_space / play_civ / play_cell / play_tribe / play_creature out/lab/20261107-232019, detect_creature + play_creature out/lab/20261107-235443, detect_creature out/lab/20261108-001833: no row of this class (Civilization:state:civ_money in 1 sample is another nation's treasury, BUG-0613 of V2).

## History
- 2026-11-01: opened.
- 2026-11-07 (fix/w46-k2a-state-b, K2a of 0.7.11): the uncovered siblings became BUG-0691, BUG-0692, BUG-0693, BUG-0694, BUG-0695, BUG-0696 (owed shared credits, the sync state over an editor visit, held guest commands, owed Space rewards, the contract clock, the cause of a total); all fixed (unreleased) with FAIL / PASS runs. Umbrella left open for the orchestrator.
- 2026-11-08 (fix/w46-k2a-state, K2a of 0.7.11): merged; children BUG-0691..0696 fixed; umbrella closed.
