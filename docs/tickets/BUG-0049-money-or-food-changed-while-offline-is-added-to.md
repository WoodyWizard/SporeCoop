---
id: BUG-0049
title: money or food changed while offline is added to the next host's shared pool
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#27
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-civ
commits: caae221, 5fb37c8
tests: tools/tests/coop_civ_shared.py (suite civ_shared)
signature:
docs: docs/re/civ.md#civ-shared-pool-sessions
related: #27
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/27 (full text, discussion).

The shared resources (tribe food, civ money, space money) keep their sync state across a session end. A player who leaves or is disconnected, changes their resources alone (plays on, or loads another save), and then joins a session again sends the whole difference to the new host as an "earning" or "spending". The host's shared pool jumps by that amount, up to 1e7. Severity: wrong behaviour (shared economy corrupted; free money).

### Where
- `shared::tick` returns before the loop while the role is None, so `have` / `synced` are never reset offline: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L141-L187
- The host adds every `MSG_RES_ADD` below 1e7: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/shared.cpp#L189-L216
- `on_session_change()` runs only from `player_join`, which on a guest comes after WELCOME: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1508-L1511
- No session-end path calls it: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1491-L1507 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771

### How to reproduce
1. Two instances, `civ` fixture: host H and guest G together, money shared (`coop.res` shows the same value on both).
2. G: `coop.leave`.
3. G: `coop.res add money 40000` (or load another civ save with more money).
4. G: join H again from the co-op window.
5. H: `coop.res` shows its money +40000 and `from_guests=+40000`. The game's own money counter on H's screen is higher by that amount.

### Expected
A guest that joins waits for the host's first total (`r.have = false`). Nothing earned or spent outside the session is sent.

## Root cause
**What happens**

1. In a session, a guest in the Civ stage has `r.have = true`, `r.synced = 5000` (the host's last total).
2. The session ends (host left, timeout, or the guest's Leave). With role None, `tick` returns at `if (s.role() == net::Role::None) return;`, so `r.have` and `r.synced` stay as they were, even across mode changes and loads.
3. The player plays alone, or loads another save with, say, 50000 money.
4. The player joins a host. `Session::join` sets the role to Client at once, and HELLO goes out as reliable message 0.
5. On the next frame, before WELCOME, `shared::tick` runs the guest branch: `r.have` is still true, `d = v - r.synced = 45000`, `pending += d`, and `send_reliable_to(0, MSG_RES_ADD)`. That works while connecting, because the client's peer 0 exists. It is reliable message 1, right after HELLO.
6. The host handles HELLO (the peer is accepted), then `MSG_RES_ADD`: `r.get(cur)` succeeds whenever the host has a civilization, and `r.set(cur + 45000)`.
7. Only then does WELCOME arrive and `player_join` call `on_session_change()`.

The same happens to a former host that later joins someone (its `synced` is its own last broadcast), and with tribe food and space money.

### Confidence
CONFIRMED by reading the full path, including the reliable ordering (HELLO then RES_ADD) and the host accepting messages right after HELLO in the same stream. Not run in game.

## Fix
Fixed 2026-10-28 (fix/w2-civ, mods/coop/shared.cpp): the sync state of every shared resource (`have`, the unsent and in-flight changes, the host's per-guest counts) ends with the session: `shared::on_session_end()` runs from `end_session_modules` and from `tick` while the role is None, and a guest that is still connecting (no WELCOME yet) resets it every frame and sends nothing. The first host total after WELCOME is the guest's new base, so nothing earned or spent outside the session reaches the host. Switch: `coop.res fix reset 0` restores the code before (tests).

Suggested in the review:

Call `shared::on_session_change()` (and clear `pending`) in `do_join`, `do_host`, `do_leave` and the disconnected callback, or in the common session-end helper of the umbrella issue. As a second guard in `shared::tick`: while `!s.connected()`, set `r.have = false; r.pending = 0;` and do not send. Keep the old behaviour behind `coop.fix shared_reset off`.

## Verification
Two-game test `tools/tests/coop_civ_shared.py` (civ fixture, part 2): the guest leaves, earns 40000 offline (`coop.res add money 40000`), joins again; the host's game money (`mWealth` as `coop.res` reads it) is sampled for 12 s.
- FAIL with `--nofix` (lane 7, out/lab/w2civ-manual/shared_nofix.log): `host money before the rejoin -50.0, highest after it 39950.0` (+40000).
- PASS with the fix: out/lab/w2civ-manual/shared_fix1.log (+0, and the guest takes the host's total after the rejoin), lab run out/lab/20261027-225434 (civ_shared OK).

Planned in the review:

`tools/tests/coop_shared_rejoin.py` (two instances, civ fixture): do the steps above, then assert the host's money as the game shows it (`cCivilization::mWealth` through `coop.res`, the game value, not the `from_guests` counter) is unchanged by the rejoin, and equals the guest's after the first total. It must fail on main and with the switch off.

### Related
- The umbrella issue on inconsistent session-end paths.
- Same "state not reset across sessions" pattern: the stale vote, civ roles and creations by player id.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #27.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-civ (sync state reset at the session end and while connecting; coop_civ_shared FAIL without, PASS with).
- 2026-10-28: released in 0.7.3.0.
