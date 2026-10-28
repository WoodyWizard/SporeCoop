---
id: BUG-0098
title: ending the "ship disabled" state writes to the player's ship even after Space was left
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#76
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-space
commits: 30a25b0
tests: space_rescue, space_stale (regression only)
signature:
docs:
related: #76
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/76 (full text, discussion).

When the rescue state ("ship disabled") ends because the player left Space or the session ended, the mod clears the ship's invincible byte through `cSimulatorPlayerUFO::Get()+0x40`, without checking that the game is still in Space. Outside Space that pointer may be stale. If so, this is a one-byte write into freed memory. Severity: minor or possible crash (not verified whether the pointer is stale outside Space).

### Where
- `down_tick`, the reset path: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L202-L212
- `on_session_end`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L887-L893
- `my_ufo`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/spacecoop.cpp#L71-L75

### How to reproduce
1. Host and guest in Space, close together.
2. The guest takes a lethal hit (`coop.space hit 1000`): "SHIP DISABLED".
3. Within 20 s, the guest quits to the galaxy menu or loads another save (or `coop.leave`).
4. The write happens in the next frame. With page heap / a debug heap in Wine the write into a freed block is reported; without it nothing visible may happen.

### Expected
No write into the ship unless the game is in Space and the pointer is the live player UFO.

## Root cause
**What happens**

```cpp
void* u = my_ufo();                       // singleton +0x40, read in any mode
if (!u || !in_space() || !connected()) {
    g_down = false;
    if (u)
        F<uint8_t>(u, kInvincible) = 0;   // written even when !in_space()
    return;
}
```

1. `g_down` is set (the player's ship is disabled, 20 s).
2. The player leaves Space within those 20 s: quit to the main menu, load another save, or go into an editor. Or the session ends (`on_session_end` does the same write).
3. The next `down_tick` sees `!in_space()` and writes 0 to `u + 0x588`. `u` comes from the `cSimulatorPlayerUFO` singleton, which may outlive the Space game.
4. docs/re/space.md 9.4 shows the game frees UFOs on its own, and CLAUDE.md section 2 says to expect anything to be freed by a stage change or a load.
5. Whether `+0x40` is cleared when the ship is freed is not known.

### Confidence
PLAUSIBLE. The code path is confirmed by reading. That the singleton's +0x40 still points to a freed ship after leaving Space is not verified.

## Fix
Fixed in spacecoop.cpp: `clear_down_invincible()` writes the invincible byte only when the game
is in Space and the singleton's ship is in the UFO noun list and not destroyed; used by `down_tick`
and `on_session_end`. Outside Space only `g_down` is cleared (a new ship starts without the byte).

## Verification
Written argument plus regression tests (2026-10-27): whether `cSimulatorPlayerUFO +0x40` is stale
after leaving Space is still not known, and a one-byte write into a freed block is not visible
without a heap checker, so no test can fail on main. The new guard reads only the UFO list
(`game::ufo_listed`), never the object, before the write. The rescue still ends with the byte
cleared in Space: `space_rescue` and the two disables of `space_stale` passed in 20261027-190159.

The original plan:

`tools/tests/coop_space_rescue.py`, a new case:
1. Disable the guest's ship next to the host.
2. Quit to the galaxy menu on the guest within 5 s, then load the space save again.
3. Assert both games are alive with no crash report, and that the guest's new ship takes damage (HP drops on `coop.space hit 10`, so it is not stuck invincible).

Run it on a lane with Wine's heap checking (`WINEDEBUG=warn+heap`) so that a write into freed memory is reported. The test should fail on main under heap checking if the pointer is stale, and pass with the fix. If it passes on main as well, record that negative result in docs/re/space.md (the pointer is cleared) and close this issue.

### Related
- docs/re/space.md 7.2 (rescue), 9.4 (freed UFOs).
- CLAUDE.md section 2 (object lifetime).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #76.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-space (wave 1, Space).
- 2026-10-28: released in 0.7.3.0.
