---
id: BUG-1054
title: Tribe: after a session that ended during the host's live Creature to Tribe change, the host reloads its own save at the first share of a later session
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) WLD-9
role: host
versions: 
fixed_in: unreleased
branch: fix/w49-life
commits: f2f013ec, a49f379c, ade567a3, a7f75467, 398a5ab1
tests: tribe_host_reload_reset
signature:
docs: docs/known-issues.md#issue-tribe-stale-host-reload, docs/01-architecture.md#arch-lifecycle-registry, docs/06-testing.md#testing-lifecycle-reset
related: BUG-0332
---

## Report
Audit 2026-11-12 for 0.7.14, finding WLD-9. Not seen at runtime, found by code reading.
After a live Creature -> Tribe change the host loads its own share save once it went out to every guest (BUG-0332,
tribe.host_reload). If the session ends before that share went out (every guest left in the seconds after the change,
or the host pressed Leave), the "reload due" flag stays set. In a later session the first world share that goes out
while the host plays Tribe makes the host's game load its own save again: an unexpected reload of the host's game a
few seconds after a guest joined (the guests see the host "resyncing").

## Root cause
Observed in code (main efd2e0ba), confidence high for the mechanism:
* mods/coop/world.cpp:615 `static bool g_reload_due;` set by host_reload_after_share (world.cpp:617, from coop.cpp:2800).
* It is cleared only when a share went out with g_pending empty (world.cpp:1381). world::reset_shares (world.cpp:683,
  the session end) clears g_pending and g_out but not g_reload_due; world::on_session_end (world.cpp:701) neither.
* g_reload_at / g_reload_left (world.cpp:625-626) are also kept, but host_reloading (world.cpp:627) ends them after
  60 s or the next Tribe frame after leaving Tribe: a stale PF_RESYNC flag for at most 60 s (low).

## Fix idea
Clear g_reload_due and g_reload_at in reset_shares / on_session_end (lifecycle reset package, BUG-1055's registry).
Switch idea `coop.fix tribe.host_reload_reset off`.

## How to test
twoinst test on the creature fixture near the Tribe change (or a test command that sets the flag through
host_reload_after_share): the guest leaves before the share goes out; then a new join while the host plays Tribe.
Assert on the host's game that no load starts (the game's mode stays Tribe without a load screen, `host_reloads=` 0).
Main: the host's game loads its save: FAIL.

## Fix
`g_reload_due` is entry `world.host_reload` of the lifecycle registry with scope world only: it is forgotten when the
host's world changes (a load, another stage) before the share went out. Not at the session end, against the ticket's
fix idea: in the same live Tribe world the host's ground still differs from the share save's, so the reload at the
first share of a later session there is BUG-0332's fix doing its job, not a stale reload. Switch
`coop.fix tribe.host_reload_reset off` (0.7.13). Test command `coop.tribe reload [arm]` (host: make the reload due as
the live change does; print host_reloads).

## Verification
Test `tribe_host_reload_reset` (the reload made due, the host leaves, loads the tribe fixture again, hosts, the guest
joins: no load of the host's game in the 40 s after the join).
FAIL (SPORE_FIX_OFF=tribe.host_reload_reset): out/lab/20261113-012110 (host_reloads 0 -> 1 after the join).
PASS: out/lab/20261113-011705, out/lab/20261113-012647.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-life (package L1, the lifecycle registry of BUG-1055).
