---
id: BUG-0196
title: Space trade: open ends (space_trade.cpp, 2026-10-23)
status: open
kind: feature
stage: Space
reported: 2026-10-23
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0542
---

## Report
A list of limits and open ends of a feature, kept as one ticket.

Copied from docs/known-issues.md, where the entry stays up to date:

* **Rivals mode is tested only in a local merge**: space_rivalry.cpp (feature 10) is not in this
  branch; coop_space_trade.py passed its rivals part (offer, accept by a real click, both own
  budgets, an open offer declined when the host ends rivals mode) on a throwaway merge with
  feat/space-rivalry 2515362 (lane 10, GOG 2024, 2026-10-23). Without space_rivalry the test
  prints "rivals mode: UNTESTED".
  ~ superseded (2026-10-27): space_rivalry.cpp is on main and coop_space_trade.py runs its rivals
  part (`rivals()`), BUG-0153.
* **The guest's UI clicks do not arrive on the new space fixture (GOG 2024, lane 10)**: after the
  join the guest's game sits in a HUD-less movie state (`coop.space hp` shows `movie=1`, Enter
  does not end it), the co-op window opens with F9 but a click on "Space >" changes nothing. The
  same state makes coop_space_page / coop_space_death / coop_space_rescue fail on lane 10, also on
  the plain feat/space-map build (not caused by space_trade). coop_space_trade.py therefore lets
  the host do every real click and presses the guest's offer row through `coop.space press`.
  ~ superseded (2026-10-27): the movie state is fixed (docs/re/space.md 7.9, see "Space (GOG 2024):
  zooming in" above); the test still presses the guest's row through `coop.space press`, BUG-0153.
* **A gift or sale on its way when the own session ends comes back** (the sender cannot know
  whether the partner already got it): leaving right after "Give" can duplicate the charges.
  A partner who leaves before answering is handled (the sender gets it back).
* **Demand cap is per seller game**: the 30 units / 10 minutes of a partner colony are counted by
  the seller and forgotten on a restart; two sellers each get their own 30.
* **The F9 Space page has 9 rows for every feature together** (coop_ui.cpp kSpaceRows): the trade
  rows (gift, next tool, other partner, sale, offers) come after the features registered before
  it and can fall off the page when many features show rows at once.
  ~ superseded (2026-10-27): the page has 12 rows with paging since 0.7.0 (coop_ui.cpp
  `kSpaceRows`), BUG-0153.
* Spice values are the game's own (`spacetrading~` 0x34F1A4A x the difficulty multiplier, about
  5 600 to 24 000 per unit on normal), so one sale of 10 is worth 110 000 to 490 000 at 2x: big
  next to the fixture's $100 000. NPC stores pay similar money for the spices they want.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-23: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-11-04 (fix/w42-space-play): "a gift or sale on its way when the own session ends comes back" fixed as
  BUG-0542 (trade.delivered, test space_trade_end): a gift or sale the partner's side acknowledged stays with the
  partner. Still open from this list: the leftover that did not fit is lost when its answer is lost; a partner who
  leaves right after receiving is still refunded by the "partner left" path (its BYE may overtake its answer, not
  seen); the demand cap per seller game (two sellers get 30 each, forgotten on a restart); NPC spice prices are big
  next to the fixture's money (by design, the game's values).
