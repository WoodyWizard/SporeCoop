---
id: BUG-0037
title: one lost ship-state packet makes up to 18 of the host's ships explode and respawn on the guest
status: fixed
kind: desync
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#15
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-space
commits: c5fe0b8
tests: space_fleet
signature:
docs: docs/re/space.md#space-ship-state-chunks
related: #15
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/15 (full text, discussion).

With more than 18 NPC ships in the host's place (a raid wave, a colony with turrets and defenders), the host sends the ship state in several unreliable chunks. If one chunk is lost or arrives late, the guest treats every ship of that chunk as gone. It blows them up with the game's own death (explosion, loot) and spawns them again 0.1 s later. Severity: wrong behaviour, visible on any real internet link during fights.

### Where
- Host sends the chunks unreliably: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L208-L234
- Guest collects the ids of one cycle and runs the removal pass on the last chunk: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L263-L318
- `destroy_mirror(u, true)` uses the game's death path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L249-L261

### How to reproduce
1. Host and one guest in Space, both on the host's colony planet (same place).
2. Get more than 18 NPC ships listed: a raid wave from `space_contracts` (2 crew: 2 bombers + 8 raiders, then 3 + 12) plus the colony's turrets and defenders, or `coop.fleet spawn 2` 20 times on the host.
3. Drop some UDP packets from host to guest (netem on the lane's loopback, or a test switch, see Test).
4. On the guest, `coop.fleet list` shows `removed=` and `created=` climbing while the host's ships did not die. On screen, ships explode and reappear.

### Expected
A lost or late chunk changes nothing. A mirror is only removed when the host no longer lists the ship.

## Root cause
**What happens**

1. `host_tick` (10 Hz) splits the ships into chunks of `per = 18`. Each chunk carries `cycle`, `chunk`, `chunks` and is sent with `send_unreliable_to(pl.id, net::MSG_UFO_STATE, w)`.
2. `guest_state` clears `g_cycle_ids` whenever a packet with a new `cycle` arrives. It then inserts the ids of that packet only.
3. When `chunk + 1 == chunks` (the last chunk), it walks `g_mirrors`. Every mirror whose id is not in `g_cycle_ids` is removed:
   ```cpp
   if (!g_cycle_ids.count(it->first)) {
       if (it->second.u) {
           g_mirror_of.erase(it->second.u);
           destroy_mirror(it->second.u, true);   // TakeDamage 1e6: explosion, loot
   ```
4. Nothing checks that all chunks of the cycle arrived. If chunk 0 of a 2-chunk cycle is lost, only the 1..18 ids of chunk 1 are in the set. All ships of chunk 0 are exploded with `o_TakeDamage(..., 1e6f, ...)`, the game's NPC death (explosion, loot drop 0x102BA40, docs/re/space.md 2.1 step 10).
5. The next cycle (0.1 s later) carries those ids again. `g_mirrors[id]` has no `u`, so `SpawnUFO` creates new ships and teleports them in.
6. The same happens if a stale chunk of an older cycle arrives after a newer one (it resets `g_cycle_ids` for its own cycle number).

With 1 % loss at 10 Hz and two chunks, this happens about once every 10 s during a big fight.

### Confidence
CONFIRMED by reading the full path (`host_tick` -> unreliable send -> `guest_state` removal pass). Not reproduced at runtime: the lab runs on loopback without loss, which is why the tests pass. Whether the guest can pick up the loot of the fake deaths is not verified.

## Fix
Fixed on fix/w2-space (c5fe0b8). `guest_state` keeps a bitmask of the chunks of the current cycle and runs the removal pass (and the local NPC removal) once, when every chunk of the cycle has arrived, in any order. A chunk of an older cycle (16-bit serial compare, within 100 cycles) is ignored; an older id by more than that counts as a restarted host. The cycle state is reset at the session end. `coop.fleet chunkfix 0` restores the old pass on the last chunk; `coop.fleet drop <chunk> <n>` (host) skips sending a chunk n times. `coop.fleet list` shows `stale_chunks` and `partial_cycles`.

Suggested in the review:

- Track which chunks of the current cycle arrived (a bitmask, `chunks` is at most 15 since `all_ufos` caps at 256). Run the removal pass only when all `chunks` of the cycle are in. Ignore packets of a cycle older than `g_rx_cycle` (16-bit serial compare).
- Additionally (or instead), remove a mirror only when it has been missing for a while, e.g. `now - m.seen > 0.5`. Remove it without the explosion when it was not marked dead by the host.
- Keep the old behaviour behind `coop.fix fleetchunks off` so the lab can check the test still catches it.

## Verification
Done (2026-10-28, lane 6, GOG 2024): `coop_space_fleet.py` (suite `space_fleet`), 20 host NPC ships (2 chunks), the host drops chunk 0 five times. `--without-fix` (`coop.fleet chunkfix 0`) FAILS: the guest's game replaced 17-18 of the host's ships (created 25 -> 43, removed 0 -> 18; runs /tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w2-space/fleet_nofix.log, /tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w2-space/fleet2--without-fix.log). With the fix PASS twice: the same ship objects, created/removed unchanged, partial_cycles=5 (/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/waves/fix_w2-space/fleet2_fix.log and lab run out/lab/20261027-233002 on fix/w2-space (23/23 with smoke)).

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_fleet_loss.py` on `twoinst.start('space')`:
1. Host and guest land on the same planet. The host spawns 25 pirates (`coop.fleet spawn 2` x 25) so the state needs 2 chunks.
2. Add a host debug switch `coop.fleet drop <chunk> <n>`: skip sending the next n packets of that chunk index. Use `drop 0 5`.
3. Read the guest's UFO noun list (the game's own list, `GetAllUFOs`, through `coop.fleet list` pointers and `mID` +0x24). Assert that the set of ship objects is the same before and after the drop: same pointers / mID, none destroyed or newly created.
4. Without the fix the guest's list changes (new objects for the chunk-0 ships). With the fix it stays the same. Also check that no game wrote a crash report.

### Related
- The same chunk pattern exists in `mods/coop/cellsync.cpp` (Cell stage mirrors). That one is filed separately by the Cell review.
- docs/re/space.md section 1 (mirror recipe), section 2.1 (TakeDamage death path), section 9.4 (the game frees NPC UFOs).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #15.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-space (c5fe0b8): removal only after a complete cycle, late chunks of older cycles ignored; test space_fleet FAIL without / PASS with.
- 2026-10-28: released in 0.7.3.0.
