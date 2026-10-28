---
id: BUG-0081
title: restoring a former chief's size can call into a freed citizen
status: fixed
kind: crash
stage: Tribe
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#59
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-tribe-civ
commits: ba9dc46
tests: tribe_chief_removed, tribe_play, tribe_coop
signature:
docs: docs/re/tribe.md#tribe-lifetime
related: #59
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/59 (full text, discussion).

When a citizen stops being a player's chief, `chiefs_tick` puts its old size back with a virtual call on the citizen's spatial object. The only check is `game::object_alive`, which reads the object's own memory and cannot tell a freed citizen. If the old chief was freed without a tick where it was still a member (removed mirror, load, removal by the game), the call goes through a vtable read from freed memory. Severity: possible crash (low probability).

### Where
- [tribe.cpp#L793-L805](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L793-L805)
- `object_alive`: [game.cpp#L139](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/game.cpp#L139)
- Guest mirrors are destroyed directly by `RemoveMember(c, destroy=1)` at the end of a snapshot cycle: [tribe.cpp#L571-L576](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L571-L576), [tribe.cpp#L667-L686](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/tribe.cpp#L667-L686)
- The same lesson was already learned in Civ (`exists()`): [civ.cpp#L76-L92](https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L76-L92)

### How to reproduce
1. Tribe stage, host + 1 guest. Wait until both chiefs are shown (`coop.tribe` status `chiefs=`).
2. On the host, remove the guest's chief from the tribe in one step so that the guest never gets a snapshot with its health at 0 (for example, the game's own removal of a member, or a debug command that calls `RemoveMember` on it).
3. The guest's next `guest_state` destroys the mirror. The guest's next `chiefs_tick` calls `SetScale` through the freed pointer. To see it every time, run the guest under a page heap (`gflags /p /enable SporeApp.exe` in the lane bottle): access violation in `tribe::tick`.

### Expected
The mod only calls into a citizen that is still a member of a tribe in the game's own list.

## Root cause
**What happens**

```cpp
for (auto it = g_chief_scale.begin(); it != g_chief_scale.end();) {
    ...
    if (!still) {
        if (game::object_alive(it->first))
            vcall<void>((uint8_t*)it->first + 0xC0, 0x40 / 4, it->second); // SetScale
        it = g_chief_scale.erase(it);
```

1. `g_chief_scale` keys are raw `cCreatureCitizen*` of chiefs from earlier frames.
2. A chief can disappear between two `chiefs_tick` calls without being seen as "not a chief" while still a member:
   - On a guest, `guest_state` removes mirrors whose id the host no longer sends, and "local members the host does not know", with `RemoveMember(c, true)`. This happens in the message handler, before the next `tick`.
   - A load frees every citizen. `on_world_change` does not clear `g_chief_scale` (see the separate session-end issue).
3. In the next `chiefs_tick`, the old chief is not in `want`. `object_alive` reads byte `+0x21` of the freed block:
   - If the block was reused by a live object of another type, the check passes and `vcall` reads a vtable pointer at `+0xC0` of that object and calls slot 0x40: crash or memory corruption.
   - If it was reused by a new citizen (likely: `guest_state` may spawn a member in the same call), that citizen gets the old chief's size.
4. The usual death case is mostly safe: health reaches 0 first and the chief is dropped while still alive. So this needs a removal without a 0-health snapshot, or a load.

### Confidence
CONFIRMED by reading that the only liveness check is `object_alive`, and that guests destroy mirrors in the message handler. The crash itself is PLAUSIBLE and depends on reuse of the freed block. Not run on the lab.

## Fix
Fixed on fix/w1-tribe-civ (ba9dc46): `restore_chief_scales` builds the set of the player tribe's
members (the tribe's own vector) once per `chiefs_tick` and calls `SetScale` only for an entry in it;
anything else is just erased. `on_session_end` restores the chiefs' size (the chiefs stayed 1.3x
after a session, part of BUG-0067) and clears `g_chief_scale`, `g_chief`, `g_chief_saved`, the
pincer and the epic tables; `on_world_change` clears them without a restore.
`coop.tribe fix chiefs 0` restores the old `object_alive` check (tests).

## Verification
Two-game test `tools/tests/coop_tribe_chief_removed.py` (suite `tribe_chief_removed`, tribe):
`coop.tribe spawn 4`, `coop.tribe drop_chief 1` (RemoveMember(c, destroy) on the guest's chief, as
guest_state does for a mirror), a new chief with 1.3x its game scale on both machines; then a
forced address reuse: `coop.tribe stale_chief <id>` puts another tribe's citizen where a former chief
was, and it must keep its own size in the game (spatial GetScale).

- Found on the way: right after RemoveMember the destroyed flag is already set, so the old check
  skipped a just-removed chief; the danger is the block reused later, which the forced step shows.
- FAIL with `--nofix` (out/lab/nofix-20261027/nofix2_tribe_chief_removed.log): the other tribe's citizen went
  from scale 2.54 to 2.00 (the stale entry's size), `chief_bad_calls=1`.
- PASS with the fix: lab run 20261027-185915 (2x).

### Related
- Chiefs keep 1.3x after the session ends and `g_chief_scale` is never cleared (separate issue).
- The city-alarm boost writes into freed vehicles via the same `object_alive` pattern (separate issue).
- docs/re/tribe.md, section "Co-op features (pincer, duet, healers, chief, epic)".
- CLAUDE.md section 2 (object lifetime).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #59.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-tribe-civ (ba9dc46), test tribe_chief_removed (FAIL with --nofix, PASS with the fix).
- 2026-10-28: released in 0.7.3.0.
