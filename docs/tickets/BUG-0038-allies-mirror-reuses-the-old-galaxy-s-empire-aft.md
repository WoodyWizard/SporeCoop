---
id: BUG-0038
title: allies mirror reuses the old galaxy's empire after a load and can take over a real NPC empire
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#16
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-space
commits: d0f6003, f581bf2
tests: space_allies_reload
signature:
docs: docs/re/space.md#space-ship-state-chunks
related: #16
switch: allies.reload
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/16 (full text, discussion).

When the galaxy is reloaded while a co-op session runs, `space_allies.cpp` drops what its mirror empire changed but keeps the `cEmpire` object from the old galaxy and its political id. The next apply puts that old object into the new galaxy's empire map at the old id. If a real NPC empire of the new galaxy got that id, the mirror replaces it, writes "ally" into its relationship entry and later clears it, so the cleared state can go into the player's save. Severity: wrong behaviour with possible save damage (a real empire's relation lost); a crash is possible but not shown.

### Where
- `forget(m)` keeps `m.emp` / `m.pol`: [space_allies.cpp#L386-L393](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L386-L393)
- reload detected in `unapply`: [#L396-L405](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L396-L405), in `apply`: [#L496-L499](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L496-L499), in `tick` outside Space: [#L845-L854](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L845-L854)
- `ensure_empire` puts the kept object back: [#L300-L334](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L300-L334)
- the session-end path that skips the cleanup when a new session starts: [#L855-L865](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L855-L865), [#L932-L945](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L932-L945)
- relationship and contact writes: [#L261-L291](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/space_allies.cpp#L261-L291)

### How to reproduce
Two players, allies mode, Space:
1. The guest founds a colony on a star that nobody owns, so the host gets a mirror (`coop.space allies list` on the host shows `applied=1` and the pol).
2. The host loads an older Space save of the same world while the session runs (`coop.load`, or a load from the menu), made before the mirror existed.
3. After the load, wait until the mirror is applied again and compare `coop.space allies list` with `coop.space allies empire <pol>`: the same pol is used again, and the mirror's star count includes the old galaxy's records. With a collision, an NPC empire at that pol shows the mirror's name.

Second path: two players. The guest is on a mirror planet when the session ends, quits to the menu and joins a new session.

### Expected
After a reload the mirror starts fresh in the new galaxy: a new cEmpire with an id from the new galaxy's counter. No object or star record from the old galaxy is reused, and no real empire's relationship entry is touched.

## Root cause
**What happens**

1. A mirror is applied: `ensure_empire` takes a new id with `NextPoliticalID`, creates the empire, and keeps its own reference in `m.emp`.
2. The galaxy is reloaded (a load while connected). `tick` (outside Space) or `apply` sees `empire_of(m.pol) != m.emp`, sets `applied = false` and calls `forget(m)`. `forget` only releases the planet records and clears `m.planets` / `m.stars`. `m.emp` and `m.pol` stay, and the old empire's star vector `+0x88`, ally/enemy vectors and `m.contact_before` stay as they were.
3. The next `apply` calls `ensure_empire`. `m.emp` is set, so it takes the `else` branch:
   ```cpp
   void** slot = thiscall<void**>(a_MapAt, (uint8_t*)sm + 0x150, &key); // key = old m.pol
   if (*slot != m.emp) { vcall<void>(m.emp, 0); void* old = *slot; *slot = m.emp; if (old) vcall<void>(old, 1); }
   ```
   The old galaxy's object goes into the new galaxy's map. It still lists the old galaxy's star records next to the new ones that `add_star` pushes.
4. Political ids come from the counter `cStarManager +0x1D4`, which the load restores, and NPC empires are created lazily with `NextPoliticalID` (docs/re/space.md section 15.1). So the new galaxy can give the same id to a real NPC empire:
   - NPC created first: step 3 replaces it in the map and releases the map's reference. Its stars (`+0x54` = that id) now resolve to the mirror. `set_relation(m, true)` sets contact flags 0x6 and the ally bit on that empire's relationship entry. On the next save or at session end, `set_relation(m, false)` clears war/ally bits and restores the stale `contact_before`, and the save keeps that.
   - Mirror inserted first: a lazily made NPC empire with that id finds the mirror in the map, and its star becomes the mirror's.
5. Second trigger without a load by hand: the session ends while the player is on a mirror planet, so `end_all` only sets `g_end_pending`. If the player then joins another session (which loads the host's world), `tick` clears `g_end_pending` because it is connected again, and `apply_all` runs with the stale mirrors.

### Confidence
CONFIRMED by reading the full path: `m.emp` / `m.pol` are kept through `forget`, and `ensure_empire` reinserts them. PLAUSIBLE: the id collision with a real NPC empire. Not verified: whether the save stores the counter at `+0x1D4` or rebuilds it on load, and whether the ally/enemy vectors hold references or raw pointers (raw pointers would make this a crash).

## Fix
Fixed on fix/w2-space (d0f6003). `reset_empire(m)` drops the kept cEmpire, political id and contact snapshot whenever the galaxy changed: in the three reload branches (`unapply`, `apply`, `tick` outside Space) and in `ensure_empire` when the map no longer holds `m.emp`, in every case only when the player's cEmpire is another object than the one the mirror was made in (`m.galaxy`): a save also takes the unmirrored cEmpire out of the map (the first version, d0f6003, made a new empire after every save and `coop_space_allies.py` failed "after the save the mirror is back", fixed in f581bf2). The last one is the path that mattered at runtime: going to the menu, the game saves and the save hook unmirrors (applied = false), so none of the "applied" reload checks saw the reload and `ensure_empire` put the old object back. Then a new cEmpire with `NextPoliticalID` of the new galaxy is made. The old object is not released (our reference is its last one, its destructor would release the old galaxy's records): one cEmpire leaks per reload. `coop.space allies reloadfix 0` restores the reuse. Not changed: the `g_end_pending` path (session end on a mirror planet, then a new session); after a reload it now also starts with a new empire.

Suggested in the review:

In every "the galaxy was reloaded" branch (`unapply`, `apply`, `tick` outside Space), and in `end_all`, release the kept empire and reset it:
```cpp
if (m.emp) vcall<void>(m.emp, 1);
m.emp = nullptr; m.pol = 0; m.contact_before = 0; m.rel_on = spaceext::Relation::Ally;
```
Then `ensure_empire` creates a new empire with `NextPoliticalID` of the new galaxy. Also, in the `g_end_pending` path, run `end_all` before a new session's first apply. Keep the old behaviour behind `coop.fix allies_reload off`.

## Verification
Done (2026-10-28, lane 6): `coop_space_allies_reload.py` (suite `space_allies_reload`). The host has a mirror (`allies fake`), goes to the galaxy menu, loads the fixture, the mirror is applied again. `--without-fix` FAILS: the empire map holds the old galaxy's object at the old id 800021CD (scratch log `reload--without-fix29322.log` (not kept)). With the fix PASS twice: a new cEmpire (other pointer) at a new id 800021D0 with its one star, and a second empire gets its own id. The collision with a real NPC empire was not produced: after the menu the counter `cStarManager +0x1D4` was not below the mirror's id (the save on the way to the menu stores it).

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_allies.py`, new step "reload":
1. Two lanes. The guest colonizes a free star; the host mirrors it.
2. The host runs `coop.load` of the fixture save taken before the colony, back into Space.
3. Assert on the host's game, not on mod counters:
   - `coop.space allies empire <old pol>` must be either no empire or an NPC empire with its own name, not "<guest>'s empire";
   - the new mirror's pol must be the value the new galaxy handed out;
   - the mirror's stars must be exactly the mirrored stars;
   - `coop.space diplomacy table` must show the NPC at that pol with the flags it had in the save.

This must FAIL on main and with `coop.fix allies_reload off`, and PASS with the fix. Also check that neither game wrote a crash report.

### Related
- docs/known-issues.md "Space allies: what the mirror of a partner's colonies does not do" (it says the political id counter is not given back).
- docs/re/space.md 15.1 (lazy empires, `NextPoliticalID`), 15.3 (contact flags), 15.4 (save).
- docs/re/space.md 8 (loads while a stage runs).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #16.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-space (d0f6003): new cEmpire after a reload (also via ensure_empire); test space_allies_reload FAIL without / PASS with.
- 2026-10-28: f581bf2: the reset only when the player's cEmpire changed (a save also drops the mirror from the map); space_allies_reload FAIL without / PASS with again.
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: log paths of an agent's scratch folder replaced by the log names (BUG-0215: no paths outside the repository).
- 2026-10-28: switch: allies.reload (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
