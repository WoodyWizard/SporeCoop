---
id: BUG-0220
title: Core: shared money or tribe food spent by a guest right when another player joins comes back (spent twice)
status: fixed
kind: desync
stage: All
reported: 2026-10-28
source: GitHub #154 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: unreleased
branch: fix/w5-core
commits: c4f4981
tests: civ_shared (coop_civ_shared.py step 3, --only-join; --nofix fails), net_tests test_shared_join_keep
signature:
docs: docs/06-testing.md#testing-w5-core
related:
switch: res.join_keep
---

## Report
From GitHub issue #154: https://github.com/WoodyWizard/SporeCoop/issues/154 (the full analysis, links and the scenario are there).

**Summary**
Every `player_join` callback resets the shared resources' sync state. This includes guests, which get the callback for each other player who joins. Until the host's next total arrives, the guest's tick records no local changes, and that total then overwrites a purchase made in the window. The money or food comes back on the guest's screen, and the host never counts the spend.

Severity LOW: the window is about one round trip per join, so it needs 3-4 players. The result is the same "spent twice" that BUG-0106 (caae221) set out to remove; this is a path that fix did not cover. The code is older than the range and shipped unchanged in 0.7.3.0.

This issue also defines the shared-ledger semantics that #165 builds on.

**Where**
- The reset on every join, host and guest: https://gi

## Root cause
Confirmed in the game (lane 4, Civ fixture): `cb.player_join` called `shared::on_session_change()` on every machine, so a guest set `have = false` for each other player's join. Until the next host total its tick recorded nothing, and that total (`led.shown`: host total + pending + in flight, not the purchase) was written back over the purchase. With `coop.res joinsim add money -500` on the guest (the join's code and the spend in one step): guest 1000 -> 500 -> 1000, host 1000, the spend never reached the host.
The editor variant (a guest's `have = false` while its game is in a Civ editor) was not checked in the game and is not changed here (step 4 of the issue).

## Fix
- `shared::on_player_join(role)`: only the host forgets its sync state on a join (the newcomer gets every total at once); a guest keeps it (`net::reset_on_join`). The newcomer itself is reset while it joins (`shared::tick` while not connected).
- Guest MSG_RES: with a host total in hand (`have`), a local change the tick has not sampled yet is folded into `pending` first (`SharedLedger::fold`), so it is reported once and not undone; after the `res.floor` early return, so `res.floor off` stays the pre-BUG-0106 code.
- Switch `coop.fix res.join_keep` (off: the reset on every machine and no fold, as 0.7.3.0). Test hook `coop.res joinsim add <name> <n>`.
- The ledger API refactor of the issue (have/synced into the ledger, `pause()`) was not done: no behaviour needs it yet; BUG-0231's shared hold, which would use `pause()`, is still open.

## Verification
- `coop_civ_shared.py --only-join` (lane 4, GOG 2024; oracle: the game's money `cCivilization::mWealth` on both machines): `--nofix` FAIL (host 1000, guest 1000 after the guest spent 500), PASS with the fix (host 500, guest 500). Logs (standalone runs, lane 4): out/lab/20261028-235900-w5-core-twins/civsh_join--nofix.log, civsh_join.log. The full test (steps 1-3) runs in the lab as `civ_shared`.
- `net_tests` test_shared_join_keep: the join decision and the fold (fails with `net_tests --nofix`).

Lab PASS: out/lab/20261028-230144 (civ_shared, steps 1-3, ok). FAIL / PASS twins: out/lab/20261028-235900-w5-core-twins/civsh_join--nofix.log (FAIL), civsh_join.log (PASS).

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): fixed: a guest keeps its sync state on another player's join and folds an unsampled change before a host total (res.join_keep); coop_civ_shared.py step 3 FAIL with --nofix, PASS with the fix.
- 2026-10-29 (fix/w5-core): after the merge of main 8f1944b: lab out/lab/20261028-235045 all passed (vote_end_editor, vote_drop_editor, away_stages, civ_other_world, civ_shared, dna_away, creature, civ_coop, fix_registry, leave_cleanup, unit).
