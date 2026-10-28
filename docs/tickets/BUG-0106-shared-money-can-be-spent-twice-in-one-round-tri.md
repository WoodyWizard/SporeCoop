---
id: BUG-0106
title: shared money can be spent twice in one round trip, and Civ/Space money goes negative
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#85
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-civ
commits: caae221, 5fb37c8
tests: tests/net_tests.cpp test_shared_ledger (make test); tools/tests/coop_civ_shared.py (suite civ_shared)
signature:
docs: docs/re/civ.md#civ-shared-pool-sessions
related: #85
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/85 (full text, discussion).

Shared resources (`shared.cpp`) work optimistically: every machine spends from its local copy, a guest reports its change (`MSG_RES_ADD`), and the host broadcasts the total (`MSG_RES`). Nothing reserves the money or puts a floor under it. When two spends cross in the network, both go through. The host applies the guest's change as `cur + v` with no floor, and the setters write the raw value into the game. Civilization money (float) and Space money (int) can therefore become negative. Food is floored per bundle, so there the overspend turns into food created from nothing.

The cause is different from #27, which is about changes made while offline.

### Where
- Host applies a guest's change with no floor: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L208-L213
- Guest applies the host's total plus its unsent change: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L203-L207
- Raw writes into the game. `s_civ` writes `mWealth` and `s_space` writes `cEmpire+0xD0`, both bypassing the game's AddMoney clamp (0..99 999 999): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L113-L116
- Food: `s_food` floors every bundle at 0 and drops whatever is still unspent: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L62-L76
- Guest batches its changes and sends at most every 0.25 s: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L170-L185

### How to reproduce
Two instances in a co-op session, both in Space (allies mode) with the same shared budget, for example §150:
1. On both consoles at the same moment (lab: send both commands in one step): `coop.res add money -100`. This goes through `shared::debug_add`, the same `r.set` path a purchase takes.
2. Wait 1 s and run `coop.res` on both. Result: `shared money=-50.0` on both, and the in-game money shows -50.

Same in Civilization with `coop.res add money -100`, where `mWealth` becomes -50.0.

### Expected
The shared total never goes below 0. A spend that the shared total cannot cover is refused or rolled back on the machine that made it. The guest's display does not jump back up by an amount it has already spent.

## Root cause
**What happens**

Case A, both players buy at once (Space, shared budget §150):
1. The host buys for §100. Its money is 50, and it broadcasts `MSG_RES 50`.
2. Before that total reaches the guest (one-way latency, plus up to 0.25 s while the guest's own change waits in `pending`), the guest buys for §100 from its local 150. The game allows it, since 150 >= 100, and the guest sends `MSG_RES_ADD -100`.
3. The host computes `r.set(50 + (-100))`, so `*p = -50` is written into the empire money. It broadcasts -50, and both players now have §-50. Two items worth 200 were bought from 150.

Case B, one guest alone, during a crossing broadcast. The host broadcasts whenever its total changes (Civ income ticks) and at least every 2 s.
1. The guest spends 100 at t0 and sends -100 at once (`pending` = 0 afterwards).
2. A host total that left before the host processed the -100 reaches the guest after t0. The guest runs `r.set(v + 0)`, and its money jumps back up by 100 for about one round trip.
3. A second purchase in that window is accepted locally and later subtracted on the host, so the total goes below 0.

Food (Tribe), both players at 20 food:
1. The host spends 15 and broadcasts 5.
2. Within the same window, the guest spends 15 (`pending` = -15) and then receives 5.
3. `s_food(5 - 15)` floors all bundles at 0, and the remaining -10 is dropped.
4. On the next tick the guest reads 0 against `synced` = -10, takes the difference of +10 as its own earning, and sends only -5.
5. The final total is 0, although 30 food was spent out of 20: 10 food was created from nothing.

### Confidence
Confirmed by reading the code; the in-game run above was not done in this review. A second reviewer verified the trace (severity low to medium: the window is about one round trip plus up to 0.25 s).

## Fix
Fixed 2026-10-28 (fix/w2-civ, mods/coop/shared.cpp, mods/coop/net/shared_ledger.h):
- The host floors a guest's change at 0 (`net::shared_add`), and the Civ / Space money setters clamp to 0..99 999 999 like the game's AddMoney. When two spends cross, the part the shared total cannot cover is lost (the purchases themselves already happened in the games; refusing them would need an undo of the game's purchase, not done).
- A guest numbers its reported changes (`MSG_RES_ADD` gets a trailing u32 sequence); the host echoes the last counted sequence of every guest after its total (`MSG_RES`: trailing `u8 n, n x (u8 id, u32 seq)`). The guest shows `total + changes not counted yet + unsent change`, so a total that left before the host counted a spend no longer gives the money back for a round trip (case B). Older peers ignore the trailing fields; with an older host the guest behaves as before.
- After applying a host total the guest takes what the game really holds as its base, so a food bundle floored at 0 is not reported as an earning on the next tick (food from nothing).
Switch: `coop.res fix floor 0` restores the code before (tests).

Suggested in the review:

- Host: `double nv = cur + v; if (nv < 0) { nv = 0; /* tell the guest: MSG_RES_REFUSED with the uncovered part */ }`, and floor the totals in `s_civ`/`s_space` (or go through AddMoney, which clamps).
- Guest: keep the changes it has sent but the host has not counted yet (`in_flight`, with a sequence number). The host echoes the last sequence it applied in `MSG_RES`, and the guest shows `v + in_flight_not_yet_counted + pending` instead of `v + pending`. This removes the jump back up in case B.
- Food: when `s_food` cannot spend all of `d`, record the shortfall instead of letting the next tick report it as an earning.

## Verification
- Unit test `test_shared_ledger` in tests/net_tests.cpp (`make test`): a crossing total with the spend still in flight shows 50, not 150; two in-flight spends with one counted; the floor. Built with `-DSPORECOOP_TEST_NOFIX=1` (the old behaviour) it FAILS 3 checks, with the fix it passes.
- Two-game test `tools/tests/coop_civ_shared.py` (civ fixture, part 1): the shared money set to 150, host and guest spend 100 in the same step. FAIL with `--nofix` (out/lab/w2civ-manual/shared_nofix.log: `host -50.0 guest -50.0`), PASS with the fix (out/lab/w2civ-manual/shared_fix1.log and lab run out/lab/20261027-225434: both 0.0, never below 0).
Not run: the Space variant (allies mode); it uses the same code path (`s_space` clamps the same way).

Planned in the review:

`tools/tests/coop_shared_race.py` on `twoinst.start`, Space and Civ fixtures:
1. Set the shared money to 150 on the host.
2. Run `coop.res add money -100` on host and guest in the same step.
3. After 1 s, assert the game's own money on both machines (the `cEmpire+0xD0` / `mWealth` reading in `coop.res`, not the mod's counters) is >= 0 and equal on both.

On main the test FAILS with -50. With the fix it PASSES with 50, and one of the two purchases is refused. A second case covers the crossing broadcast: the guest spends twice within 100 ms right after a host total, and the test asserts the money never shows the spent amount again (sample every frame).

### Related
#27 (shared pool, other cause: changes made offline), #29 (cell food dips below a level line, same optimistic sync pattern), tracking issue #82.

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #85.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-civ (floor on the host and in the setters, in-flight changes with sequence numbers; unit and two-game tests FAIL without, PASS with).
- 2026-10-28: released in 0.7.3.0.
