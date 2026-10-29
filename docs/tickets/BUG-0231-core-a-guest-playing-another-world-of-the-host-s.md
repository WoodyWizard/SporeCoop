---
id: BUG-0231
title: Core: a guest playing another world of the host's stage is handled differently by each module (Civ still applies the host's state and founds cities, Tribe freezes, DNA goes to the host)
status: open
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #165 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in:
branch: fix/w5-core
commits: c4f4981 (part)
tests: civ_other_world (coop_civ_other_world.py; --nofix fails)
signature:
docs: docs/06-testing.md#testing-w5-core
related:
---

## Report
From GitHub issue #165: https://github.com/WoodyWizard/SporeCoop/issues/165 (the full analysis, links and the scenario are there).

**Summary**
BUG-0130 (fix/w2-world, 7e2173d) was meant to stop the host's world being applied to a guest that plays another world of the host's stage. Each module decides "the guest is elsewhere" differently, or not at all:
- Civ still applies the host's vehicle and city state. It kills the guest's own vehicles and, since BUG-0120, founds cities in the guest's own world.
- Tribe treats the guest as a mirror guest, so its own tribe stands frozen and ignores orders.
- Creature DNA the guest earns in its own world goes to the host's world.
- A failed share is never retried in that phase, so the state can last.

Each part is LOW. Together they are a MED design problem: the fix needs one rule that every module follows. With default settings (`share_world=1`, `auto_load_world=1`) the window is s

## Root cause
Checked on b423f56: `world_state_msg` (coop.cpp) did not list MSG_CIV_STATE / MSG_CIV_BUILD / MSG_CIV_ALARM, so a guest in another world of the host's Civ stage ran `civ::guest_state` and `city_catch_up` on its own planet; `trc.host_away` / `cvc.host_away` were only `g_host_away`, so Tribe and Civ `local_play()` stayed false there (AI skipped, orders sent to the host). Observed (lane 4, Civ fixture, the guest in a renamed copy of the planet with `coop.worldfix autoload 0`): with the fix off the guest's own vehicle was bound as a mirror of a host vehicle within 40 s.

## Fix
Part done on fix/w5-core (switch `coop.fix core.world_gate`, off = 0.7.3.0):
- the three Civ messages are in the gate (dropped while `host_elsewhere()`, as the Creature, Cell and Tribe state since BUG-0130);
- `trc.host_away` and `cvc.host_away` are `g_host_away || host_elsewhere()`, so Tribe and Civ play on with their own AI and orders while the guest plays another world (the BUG-0151 local play).
Not done (left open, it is the redesign of the issue): the shared predicate `host_world()` with `Welcome::world`, the `host_absent_for()` function, DNA row 2 (a guest's DNA earned in its own world stays there: a visible behaviour change that needs a release note), the shared-resource hold (`res.elsewhere`, needs the ledger `pause()` of BUG-0220's design), the `city_catch_up` second guard (civ.cpp internals, fix/w5-world's area) and the share retry after a failed `world::tick` share (`core.share_retry`). Before the host's first state a joining guest still counts as "not elsewhere" for the Civ messages (`host_elsewhere(false)`), one round trip.

## Verification
`coop_civ_other_world.py` (lane 4, GOG 2024): the guest leaves, loads its own copy of the Civ planet (Otherworld), joins with autoload 0 and stays 40 s. `--nofix` FAIL (1 of its 1 vehicles bound as a mirror of a host vehicle), PASS with the fix (0 bound, still alive, still in Otherworld, `elsewhere=1`). Logs: scratchpad w5/fix_w5-core civ_other_world--nofix.log, civ_other_world.log. Weak points: the fixture planet has one vehicle, and the mirror binding is read from the census `own` column (the mod's table that drives the game's vehicle), not from the vehicle's motion; the Tribe local play while elsewhere has no game test.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): part fixed: Civ messages gated for a guest in another world, Tribe/Civ local play there (core.world_gate); civ_other_world FAIL with --nofix, PASS with the fix. Left open: host_world()/Welcome::world, DNA row 2, shared hold, share retry, city_catch_up guard (see Fix).
