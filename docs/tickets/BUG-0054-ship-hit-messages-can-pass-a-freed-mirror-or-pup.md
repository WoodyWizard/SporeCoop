---
id: BUG-0054
title: ship hit messages can pass a freed mirror or puppet into the game's TakeDamage (possible crash)
status: fixed
kind: crash
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#32
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-space
commits: 8604dd3, 48a56fc
tests: space_stale (coop_space_stale.py; --without-fix must fail)
signature:
docs: docs/re/space.md#space-stale-lookups, docs/06-testing.md#testing-space-stale
related: #32
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/32 (full text, discussion).

Two network handlers in the squadron code pass a ship pointer as the "attacker" into the game's `TakeDamage` without checking that the ship still exists. The game frees NPC UFOs, our mirrors and our puppets on every context switch (docs/re/space.md 9.4). A message that arrives in the frame right after such a switch therefore hands a freed object to the game, which reads it and AddRefs it. Severity: possible crash (use after free) or heap corruption.

### Where
- Guest, MSG_UFO_DAMAGE: the mirror pointer `a` is used unchecked: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L400-L418
- Host, MSG_UFO_HIT: the target is checked with `live()`, the guest's puppet is not: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L386-L399
- `puppet_of` returns the raw `Remote::puppet`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L2896-L2899
- Frame order: messages are handled in `g_sess.update()` before the ticks that drop freed pointers: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1089-L1096 and https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1150-L1158
- The check that exists but is not used here: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L240-L247

### How to reproduce
1. Host and guest on the same planet, pirates attacking the guest's ship (the host's NPCs hit the guest's puppet; `coop.fleet hurt <ship> <player>` on the host sends MSG_UFO_DAMAGE on demand).
2. While hits are coming in, the guest zooms out to the system view.
3. The window is the one frame between the free and the guest's next `squadron::tick`. Lag makes it more likely, because retransmitted reliable messages arrive in bursts.
4. The result is an access violation in TakeDamage / AddHostileUnit (0xC3CEA0 / 0xC37AE0 area) or a corrupted heap later.

### Expected
A message about a ship that no longer exists is applied without an attacker, or dropped.

## Root cause
**What happens**

Guest side:
1. The host's NPC hits the guest's puppet on the host. The host sends MSG_UFO_DAMAGE (reliable) to the guest with the attacker's net id.
2. Meanwhile the guest zooms out of the planet. The game's context switch (`DestroyNPCUFOs`) frees the planet's UFOs, the guest's mirrors included.
3. Next frame, `on_frame` calls `g_sess.update()` first. The handler runs:
   ```cpp
   auto it = g_mirrors.find(att);
   void* a = it != g_mirrors.end() ? it->second.u : nullptr;   // freed
   ...
   h_TakeDamage((uint8_t*)me + kCombat, dmg, a ? F<uint32_t>(a, kPol) : 0xFFFFFFFFu, t, dir,
                a ? (uint8_t*)a + kCombat : nullptr);
   ```
   `F<uint32_t>(a, kPol)` reads freed memory. With `g_applying` set, `h_TakeDamage` goes straight to `o_TakeDamage` with the freed attacker. Per docs/re/space.md 2.1 the game then checks whether the attacker is destroyed (reads it) and calls `AddHostileUnit(attacker)`, which AddRefs it (writes to it).
4. `guest_tick`, which would have set `m.u = nullptr` for the freed mirror, runs only later in the same frame (`squadron::tick`).

Host side:
1. A guest shoots a host ship. The host gets MSG_UFO_HIT and checks the target with `live()`, but takes `puppet = C.puppet_of(from)` as is and passes `puppet + kCombat` as the attacker.
2. `Remote::puppet` is reset only in `drive_puppet` when `space_alive` fails (coop.cpp, `puppet_adapter->alive`), again after `g_sess.update()`.
3. If the puppet was freed (context switch, or a puppet killed in rivals mode) while the target is still listed, the game gets a freed attacker. This needs the target to survive while the puppet is freed, so it is rarer than the guest case.

### Confidence
PLAUSIBLE. The code path is confirmed by reading: the pointer is not checked, and the handler runs before the tick that clears it. That the game frees mirrors and puppets on leaving a planet is documented at runtime (docs/re/space.md 9.4). Not seen as a crash yet; the timing window is one frame.

## Fix
Fixed in `mods/coop/squadron.cpp`: both hit handlers pass the attacker through one check before
the game's TakeDamage. `hit_attacker()` drops (null attacker, the hit still applies) a mirror or
puppet that is no longer in the UFO noun list, destroyed (+0x21) or dying (state 2);
`hit_puppet()` also requires the puppet to be a type-6 Defender, as `space_alive` does. The
`coop.fleet hurt` test command checks the puppet too. `coop.fleet hitfix 0` restores the old
unchecked path. Not changed: `puppet_of` in coop.cpp (shared file); the check sits at the use.

## Verification
Done (2026-10-27, lane 6, GOG 2024). Test command that forces the one-frame window:
`coop.fleet freemirror <id>` (guest) frees the mirror through the game and keeps its pointer
listed until the next MSG_UFO_DAMAGE; `coop.fleet freepuppet <p>` (host) frees the guest's
puppet and makes it the next MSG_UFO_HIT's attacker. `coop_space_stale.py` then sends the hit and
asserts the ship's hp on the receiving game drops, no fault, and `att_freed` (an attacker handed to
TakeDamage that is not in the UFO list, counted where the game is called) stays 0.
- FAIL with `--without-fix` (hitfix 0): lab run 20261027-183320 space_stale_nofix (guest att_freed 0 -> 1,
  host att_freed 0 -> 1), again in 20261027-190159.
- PASS with the fix: 20261027-183320, 20261027-183607, 20261027-190159.
No crash was seen without the fix: the freed memory was still mapped, the game read it quietly.

The original plan:

Hard to hit by timing. Make it deterministic with a test command on the guest: `coop.fleet freemirror <id>` destroys the mirror through the game (`DestroyInstance`) but leaves `g_mirrors[id].u` set, as a context switch does. Then on the host `coop.fleet hurt <id> <guest>`.
- Without the fix the guest reads the freed object: run the lane with page heap / `WINEDEBUG=+heap`, or check `sporekit.log` for a caught access violation.
- With the fix the hit is applied without an attacker: the guest's ship HP drops by the damage and the game is still alive.
- Assert on the game's own state: the guest ship's HP (+0x540), no crash report on either game.

### Related
- docs/re/space.md 9.4 ("The game frees NPC UFOs, puppets included") and 2.1 (TakeDamage order).
- CLAUDE.md section 2, object lifetime.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #32.
- 2026-10-27: ticket opened from the issue.
- 2026-10-27: fixed on fix/w1-space (wave 1, Space).
- 2026-10-28: released in 0.7.3.0.
