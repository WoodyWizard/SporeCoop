---
id: BUG-0076
title: a new player is shown with the creature or ship of the player who had their id before
status: fixed
kind: desync
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#54
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-net
commits: 9afc74d, 65922a9
tests: tools/tests/coop_creations_rejoin.py (suite creations_rejoin)
signature:
docs: docs/04-netcode.md#netcode-w2, docs/06-testing.md#testing-net-w2
related: #54
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/54 (full text, discussion).

creations.cpp keeps each player's own creation (creature, cell, UFO) per player id: `g_applied` on every machine, plus `g_store` on the host, which the host forwards to newcomers. Nothing removes a player's entries when that player leaves, and ids are reused. A new player under the same id is therefore shown with the old player's creature, at least until it sends its own, and forever if it has none ("the world's own"). The host also hands the old bundle to later joiners as that id's look. Severity: wrong behaviour (every machine shows the wrong look for the newcomer).

### Where
- `g_store` / `g_applied`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L59 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L77
- The host sends every stored bundle except the newcomer's own id: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L981-L989
- Only the session end clears them: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L991-L1006
- The puppet's species comes from `player_key(id)`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/creations.cpp#L1008-L1018 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2784-L2789
- `cb.player_leave` does not tell creations: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1707-L1717
- `do_leave` skips `creations::on_session_end`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771

### How to reproduce
1. Three instances (host, A with an own creature in My Creations, B without), `creature` fixture.
2. A joins, choosing its own creature; the host sees A as that creature.
3. A: `coop.leave`. B joins (id 1).
4. On the host, `coop.status` shows B's puppet `species=` with A's creation key, and the host's screen shows A's creature with B's name tag.

### Expected
A newcomer starts with "no own creation" on every machine (the world's species) until its own bundle arrives. The host never forwards a bundle of a player who left.

## Root cause
**What happens**

1. Guest A (id 1) has its own creature X. The host stores `g_store[{1, creature}]` and applies it: `g_applied[{1, creature}].key = X'`. Guest C (id 2) also gets it.
2. A leaves. `player_leave` despawns A's puppet only; both maps keep X for id 1.
3. Guest B joins and gets id 1 again (`alloc_id` takes the lowest free id). B plays "the world's own" creature, so it sends no bundle.
4. On the host, `creature_spawn` calls `g_species_for_player(1)`, which returns `player_species(1)` = X'. B's puppet looks like A's creature. On C the same happens through C's own `g_applied`.
5. Guest D joins later as id 3. `on_player_join(3)` forwards `g_store[{1, creature}]` = A's X, so D also shows B as X.
6. If B has its own creation, it overwrites the entry once its bundle arrives; until then the wrong look is shown and the puppet is rebuilt once more.

After the host's own "Stop hosting" and a re-host, the same happens across sessions, because `do_leave` does not call `creations::on_session_end`.

### Confidence
CONFIRMED by reading the full path. Not verified in game: whether some other path (e.g. a fresh "none" message from B) would overwrite the entry; creations.cpp has no such message for "the world's own" that I found.

## Fix
Fixed on fix/w2-net (9afc74d). `creations::on_player_leave(id)`, called from `cb.player_leave`,
erases the leaving owner's `g_store`, `g_rx` and `g_applied` entries and `g_own_sent`. Its
registered packages stay registered, as in `on_session_end` (its puppet is gone with it). The
session-end part (`do_leave` skipping `creations::on_session_end`) was already fixed by BUG-0033
(`end_session_modules`). `player_join` also drops an old remote entry of the id (BUG-0109).
Switch: `coop.corefix creations_leave 0` (with `id_reuse 0`).

The review's suggestions were:

Add `creations::on_player_leave(uint8_t id)` and call it from `cb.player_leave`. It should:
- erase every `g_applied` and `g_store` entry with `owner == id` (unregistering its package as `apply` does for a replaced one);
- erase `g_asked`, `g_rx` and `g_remap` entries of that owner.

Also run `creations::on_session_end` from every session-end path (#11). Keep the old behaviour behind `coop.fix creations_leave off`.

## Verification
`coop_creations_rejoin.py` on lane 2 (two games): the guest plays its own creature (7C329711),
chooses "the world's own", leaves and joins again as id 1. `--without-fix`: the host shows the
newcomer as 7C329711 (FAIL); with the fix the host shows it as the world's species 065CBE70
(PASS, 2 of 2). Not run with a third machine (the forward to later joiners follows from the
erased `g_store` entry).

Planned in the review:

`tools/tests/coop_creations_rejoin.py` (three instances, or two with a second guest profile):
1. A with an own creature joins, then leaves.
2. B without one joins under the same id.
3. Assert on the host and on a third machine the game's species of B's puppet (`coop.status` `species=` read from the creature, i.e. `species_key(puppet)`) equals the world's species, not A's key.

It must fail on main.

### Related
- The issue on civ roles sticking to reused ids (same pattern).
- Umbrella #11.
- `tools/tests/coop_creations.py`.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #54.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-net (wave 2, netcode); tests above FAIL without the fix and PASS with it.
- 2026-10-28: released in 0.7.3.0.
