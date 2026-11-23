---
id: BUG-1472
title: Creature: a partner revives a downed player; the cost is the downed player's own (session setting creature.revive; idea from PR #214 by IltonBJSilva)
status: open
kind: feature
stage: Creature
reported: 2026-11-23
source: plan, PR #214
role: both
versions: 0.7.17
fixed_in:
branch: feat/w71-revive
commits: 32d709bf
tests: creature_revive, creature_revive_price_wallet, creature_revive_price_progress, creature_revive_price_shared, creature_revive_first_line, creature_revive_wipe, creature_revive_paths, creature_revive_old, creature_respawn
signature:
docs: docs/03-gameplay.md#gameplay-revive, docs/04-netcode.md#netcode-rescue, docs/re/creature.md#creature-revive, docs/06-testing.md#testing-revive
related: BUG-1173, BUG-0099, BUG-1469
switch: creature.revive_hold, creature.revive_debt
---

## Report
A lethal hit while a partner is in the world downs the creature (1 hp, held, 60 s); a partner within 6 m for 1.5 s revives it at 40 %; otherwise it dies the game's way. Cost charged to the downed player (own wallet, own progress debt, or the shared pool). Wire (creature 1.3, MSG 77 RESCUE), the debt in the `progress` record, switches and tests: docs/plan-0.7.17.md item 4. Credit IltonBJSilva (PR #214).

## Root cause
A feature (no bug). Design: docs/03-gameplay.md#gameplay-revive. Ported as an idea from PR #214 by IltonBJSilva (its
Creature part, BUG-9283 there: the rule header creature_rescue_logic.h, K_DOWN / K_RESULT / K_LINES, the party wipe,
the "DOWN" tags, give-up by holding G, the kill retries during a pausing caption); not merged. Changed against #214:
* the pack rescue (BUG-0099) stays the first line, the knockout comes after it (owner question 4, provisional: the order
  is revive_logic.h catch_hit, unit tested); #214 put the knockout before it;
* the price is the downed player's own, never 10 % of the host's DNA: own wallet its wallet, shared the shared pool, own
  progress a debt on its bar in the `progress` record's debt column, paid from its next own gains (#214 charged the
  host's total);
* the respawn is the game's at the shared nest (#214 needed its own nests of #208, Rivals);
* creature 1.3 and the reserved id 77 (#214: creature 1.2 and id 95); old-peer guards on both sides;
* the setting `creature.revive` (#214: fix switches only); #214's `creature.down_60` (30 s off) is not ported.
Observed vs inferred: the game behaviour behind the hold and the kill retries is PR #214's runtime (lane D, Steam build);
not re-checked by us yet (docs/re/creature.md#creature-revive, marked ~).

## Fix
Branch feat/w71-revive. `mods/coop/revive_logic.h` (rules, price, debt payment, wire; tests/revive_tests.cpp),
`revive.cpp` / `revive.h` (the hold, the host's windows, the verdicts, the price, the lines, kill retries, `coop.revive`),
`give_up.h` (hold G), teamplay.cpp on_own_damage (guard, pack rescue, then down_now; no pack regen while down),
own_progress.cpp (the debt: local_debt, pay_debt in dna.cpp on_dna and take_gain, charge_debt, MSG_PROGRESS kDebt, the
host's own debt under the store key `host`), coop.cpp (tags, arrows, the context), vote.cpp save_blocker (no world save
while a window is open), session_glue.cpp (route, leave), settings_core.h (`creature.revive`, default
`settings::kReviveDefault` = on, provisional), messages.def MSG 77 RESCUE (creature 1.3 re-locked with --rebase).
Switches: the setting `creature.revive` (off: 0.7.16.1 exactly), `coop.fix creature.revive_hold` (off: only the downing
hit is dropped, the next one kills), `coop.fix creature.revive_debt` (off: #214's rule, 10 % of the host's DNA).

## Verification
open, waiting for the lab run. pending night run: creature_revive, creature_revive_price_wallet,
creature_revive_price_progress, creature_revive_price_shared, creature_revive_first_line, creature_revive_wipe,
creature_revive_paths, creature_revive_old, creature_respawn (adapted: gives up after the second lethal hit),
team_real_damage; twins that must FAIL: creature_revive_setting_off, creature_revive_hold_off, creature_revive_debt_off.
Unit tests: tests/revive_tests.cpp (make test, PASS), validate_tests (the settings row, MSG 77).

## History
- 2026-11-23: opened.
- 2026-11-23: planned for 0.7.17 in WP-0: docs/plan-0.7.17.md item 4 (WP-R1), branch feat/w71-revive.
- 2026-11-23: implemented on feat/w71-revive (credit IltonBJSilva, PR #214); lab tests and twins written, night run pending.
