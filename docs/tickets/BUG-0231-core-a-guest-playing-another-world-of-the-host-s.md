---
id: BUG-0231
title: Core: a guest playing another world of the host's stage is handled differently by each module (Civ still applies the host's state and founds cities, Tribe freezes, DNA goes to the host)
status: fixed
kind: bug
stage: All
reported: 2026-10-28
source: GitHub #165 (review of release 0.7.3.0, 5801eea)
role: all
versions: 0.7.3.0
fixed_in: 0.7.3.2
branch: fix/w5-core, fix/w9-core
commits: c4f4981, 9202d71 (part, 0.7.3.1), c2e38eb, d4df16d, 6c28f39, 23e2e05
tests: civ_other_world (coop_civ_other_world.py; --nofix and SPORE_FIX_OFF=res.elsewhere fail), tribe_other_world (--nofix fails), join_other_world (SPORE_FIX_OFF=core.world_dna fails), share_retry (SPORE_FIX_OFF=core.share_retry fails), make test (validate_tests test_host_world)
signature:
docs: docs/06-testing.md#testing-w5-core, docs/06-testing.md#testing-w9-core, docs/07-modes.md#modes-host-world, docs/11-rivals.md
related: BUG-0130, BUG-0120, BUG-0151, BUG-0220, BUG-0233, BUG-0261
switch: core.world_gate, core.world_dna, res.elsewhere, core.share_retry
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

Checked on 7a1c49b (0.7.3.1) for the rest: `on_dna` forwarded every positive gain of a connected guest to the host without asking where the host was (DNA earned in the guest's own world went to the host's world); `shared.cpp` applied `MSG_RES` totals and sampled local changes whatever world the guest played (its own Civ money became the host's total: lab 20261030-002941, closest guest/host 0.0); `follow_phase` / `player_join` set `world_sent` before `world::share_with`, and a share that failed later in `world::tick` cleared nothing, so the guest was skipped for the rest of the phase (lab 20261030-002615: the guest stayed in Otherworld 120 s); `Welcome::world` was sent but never set, so a joining guest could not tell the host's world before its first state. Observed in the lab runs named in Verification.

## Fix
Part done on fix/w5-core (switch `coop.fix core.world_gate`, off = 0.7.3.0):
- the three Civ messages are in the gate (dropped while `host_elsewhere()`, as the Creature, Cell and Tribe state since BUG-0130);
- `trc.host_away` and `cvc.host_away` are `g_host_away || host_elsewhere()`, so Tribe and Civ play on with their own AI and orders while the guest plays another world (the BUG-0151 local play).
Not done (left open, it is the redesign of the issue): the shared predicate `host_world()` with `Welcome::world`, the `host_absent_for()` function, DNA row 2 (a guest's DNA earned in its own world stays there: a visible behaviour change that needs a release note), the shared-resource hold (`res.elsewhere`, needs the ledger `pause()` of BUG-0220's design), the `city_catch_up` second guard (civ.cpp internals, fix/w5-world's area) and the share retry after a failed `world::tick` share (`core.share_retry`). The Civ messages are also dropped before the host's first state while gated (`host_elsewhere(true)`): in the lab one cycle applied in that window bound the guest's vehicle for good (run 20261028-230144); a guest in the host's world gets the next cycle.

Rest done on fix/w9-core (design: docs/07-modes.md#modes-host-world):
- `mods/coop/host_world.h` (pure, unit tested): `classify()` NotGated / Same / Other, with the semantics of the issue; coop.cpp `host_world_now()`, `guest_elsewhere()` and `host_absent_for(Absent::Npc|Tribe|Civ)`; the npc, tribe and civ contexts call `host_absent_for` (npc keeps `core.same_place`, Tribe/Civ `core.world_gate`). The gate (`gate_drops`) asks `guest_elsewhere()` for every gated type (off: 0.7.3.0's `host_elsewhere(type == MSG_DNA)`).
- `Welcome::world`: the host sets `g_sess.world` every frame before `update()` (save CRC in a gated stage, else 0), the guest keeps it (`Session::welcome_world()`, reset by `leave()`). A guest joining in the host's world is Same from WELCOME on, so it no longer drops the host's first Civ cycle / one-shot messages (the leftover of 9202d71). Not switched (older guests never read it).
- DNA row 2 (`core.world_dna`): `dna_route.h` has rows 1-8 and 12 as pure functions (`guest_gain`, `host_total`, `host_add`); a gain while elsewhere is applied in the guest's own world and nothing is sent; the host's total (kept as `g_dna_pending`) replaces it once the guest plays the host's world. Release note in CHANGELOG [Unreleased] "Changed".
- Shared hold (`res.elsewhere`): `shared::tick(..., elsewhere)`; while held `have` is false, `SharedLedger::pause()` keeps pending and in-flight changes, `MSG_RES` is ignored (`take_total`), nothing is sent; the first host total back in the host's world ends the hold. No ledger reset.
- `city_catch_up`: `if (local_play()) return;` as the second guard (no switch: it only acts behind the gate).
- Share retry (`core.share_retry`): `world::share_with(..., tag)` keeps the guest's `join_serial`; the final failure in `world::tick` calls `world::set_share_failed` (also a synchronous failure); coop.cpp ignores another serial (a newcomer on the id) and "planet missing", clears `world_sent` (sets `needs_world` and resets the cinematic timer for a share of the join) and follow_phase retries 30 s later, at most 3 times per phase and guest, then "cannot share the world with <name>: use coop.share". Test hook `coop.worldfix failnext`.
- Test hooks: `coop.tribe select_own <n> <0|1>`; `coop.status` "own-world dna gains, welcome world, share retries"; `coop.res` `held=`.

## Verification
`coop_civ_other_world.py` (lane 4, GOG 2024): the guest leaves, loads its own copy of the Civ planet (Otherworld), joins with autoload 0 and stays 40 s. `--nofix` FAIL (1 of its 1 vehicles bound as a mirror of a host vehicle), PASS with the fix (0 bound, still alive, still in Otherworld, `elsewhere=1`). Logs (standalone runs, lane 4): out/lab/20261028-235900-w5-core-twins/civ_other_world--nofix.log, civ_other_world.log. The lab then failed it with the first version of the fix (20261028-230144: the state of the round trip before the host's first PlayerState was applied); with the second commit PASS 3/3 (lab 20261028-231857) and `--nofix` still FAIL (civ_other_world--nofix2.log). Weak points: the fixture planet has one vehicle, and the mirror binding is read from the census `own` column (the mod's table that drives the game's vehicle), not from the vehicle's motion; the Tribe local play while elsewhere has no game test.

FAIL twin: out/lab/20261028-235900-w5-core-twins/civ_other_world--nofix.log and civ_other_world--nofix2.log (with 9202d71).

fix/w9-core (lane 4, GOG 2024):
- `coop_share_retry.py`: FAIL with `SPORE_FIX_OFF=core.share_retry` out/lab/20261030-002615 (the guest stayed in Otherworld for 120 s, host share retries 0); PASS out/lab/20261030-001953 (the guest in Scalinus 43 s after the join, 1 retry).
- `coop_civ_other_world.py` step 4: FAIL with `SPORE_FIX_OFF=res.elsewhere` out/lab/20261030-002941 (the guest's own money became the host's total, closest 0.0); PASS out/lab/20261030-001953 (777 apart all 40 s, host drift 0).
- `coop_join_other_world.py` step 5: FAIL with `SPORE_FIX_OFF=core.world_dna` out/lab/20261030-003137 (guest 0 -> 0, host 50 -> 70); PASS out/lab/20261030-001953 (guest 0 -> 20, host 50 -> 50; after the share the guest has the host's 50).
- `coop_tribe_other_world.py`: FAIL `--nofix` (standalone, lane 4: own member moved 0.0 m); PASS checks out/lab/20261030-003601 (own member walked 88 m) with the invariant "guest: the console stopped answering", the BUG-0261 signature: no crash report, the guest's console answered right after the run; the machine ran 5 lanes at ~10 fps.
- Second PASS: out/lab/20261030-003942 (share_retry, join_other_world ok; civ_other_world and tribe_other_world passed every check but failed the invariant "guest: the console stopped answering", see above) and out/lab/20261030-011025 (civ_other_world, tribe_other_world: PASS 2/2, load average 27). A standalone tribe_other_world with a 1 s console poller next to it saw no gap in the guest's console: the invariant is the lab harness under load (BUG-0261), not this fix.
- Regressions in out/lab/20261030-003942, all ok: civ_city_join (BUG-0120), civ_away, tribe_away, away_editor, dna_away, away_stages (BUG-0151/0205/0233), civ_shared (BUG-0106/0049/0220), dna_resend (BUG-0128), leave_during_share (BUG-0140), world_queue, creature, civ_coop, tribe_coop, space.
- Unit: `validate_tests` test_host_world (classifier, DNA rows 1-8 and 12, ledger hold).
- no-fail-run: the `city_catch_up` guard (behind the gate, which drops the city list first; no path reaches it with the gate on).
- Not run: a mixed session with a 0.7.2.4 host (`Welcome::world` 0: Other for one round trip, as accepted in the issue); 3-4 players with one guest elsewhere; `coop_npc_multi.py`.

## History
- 2026-10-28: opened.
- 2026-10-28 (fix/w5-core): part fixed: Civ messages gated for a guest in another world, Tribe/Civ local play there (core.world_gate); civ_other_world FAIL with --nofix, PASS with the fix. Left open: host_world()/Welcome::world, DNA row 2, shared hold, share retry, city_catch_up guard (see Fix).
- 2026-10-28 (fix/w5-core): the Civ gate also covers the round trip before the host's first state (lab FAIL 20261028-230144, then PASS 3/3 20261028-231857).
- 2026-10-29 (fix/w5-core): after the merge of main 8f1944b: lab out/lab/20261028-235045 all passed (vote_end_editor, vote_drop_editor, away_stages, civ_other_world, civ_shared, dna_away, creature, civ_coop, fix_registry, leave_cleanup, unit).
- 2026-10-30 (fix/w9-core): the rest fixed: host_world.h with Welcome::world, host_absent_for, DNA row 2 (core.world_dna), shared hold (res.elsewhere), share retry (core.share_retry), city_catch_up guard; FAIL/PASS runs in Verification.
- 2026-11-01: released in 0.7.3.2.
