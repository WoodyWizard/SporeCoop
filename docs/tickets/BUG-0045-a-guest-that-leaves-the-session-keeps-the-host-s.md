---
id: BUG-0045
title: a guest that leaves the session keeps the host's ships, which then attack it with their own AI
status: fixed
kind: bug
stage: Space
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#23
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: integrate/pr5-pr6
commits: a387323
tests: leave_cleanup
signature:
docs: 
related: #23
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/23 (full text, discussion).

When a guest leaves a session in Space with the co-op window's Leave button or `coop.leave`, the mirror ships of the host's NPCs stay in the guest's game. After the leave nothing pins them and nothing blocks their weapons, so the host's pirates and empire ships become live NPCs that fight the guest with their own AI. The guest's own NPC ships, which the mod removed while it was with the host, do not come back. Severity: wrong behaviour. Whether these ships can end up in the guest's save is not verified.

### Where
- `do_leave` does not call `squadron::on_session_end`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771
- The disconnected callback does call it (only used when the host closes or the link times out): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1491-L1506
- `squadron::tick` returns early when not connected: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L370-L378
- The FireTool guard only works while `is_guest()`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L141-L158
- The cleanup that is skipped: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/squadron.cpp#L421-L430

### How to reproduce
1. Host and one guest in Space, both on the same planet or in the same system, with pirates around (`coop.fleet spawn 2` a few times on the host, or a raid).
2. On the guest, `coop.fleet list` shows the mirrors.
3. The guest presses Leave in the co-op window (or `coop.leave`).
4. On the guest, `coop.fleet list` (not connected) still lists the host's ships, and they start moving and shooting at the guest's ship.

### Expected
After a leave the guest's game is its own again: no mirror ships, as after the host closing the session.

## Root cause
**What happens**

1. While the guest is in the host's place, `guest_state` spawns a mirror for every host ship with `SpawnUFO(type, &pol, key)`. `pol` and `key` are the host's political id and model, from the host's save. It also destroys the guest's own NPC ships there (`local_removed`).
2. The player presses Leave. `do_leave` calls `g_sess.leave`, `npc::on_session_end`, `social::on_session_end`, `spacecoop::on_session_end` and despawns the puppets. It does not call `squadron::on_session_end`, which would destroy the mirrors and clear `g_mirrors`, `g_mirror_of`, `g_hid` and `g_hid_rev`.
3. From now on `squadron::tick` returns at once (`!C.sess->connected()`), so `guest_tick` no longer pins the mirrors' position, destination and HP.
4. `h_FireTool` blocks mirror fire only `if (owner && is_guest() && ...)`. `is_guest()` needs `connected()`, so the mirrors fire again. `h_TakeDamage` also passes straight to the game.
5. Result: the host's pirates, bombers and empire defenders, with political ids that may mean nothing in the guest's galaxy, now act as real NPCs in the guest's game. docs/re/space.md 10.2 notes that ships made by `SpawnUFO` are not freed by a context switch (they came along from the system view to a planet), so they can stay for a long time.
6. The stale maps stay until the next session. On a rejoin `guest_state` may reuse an old `g_mirrors[id]` whose ship is still listed for a different host ship (old type and model).

`spaceext::session_end_all` does run (through `spacecoop::on_session_end`), so the Space features themselves are reset. Only the squadron is missed.

### Confidence
CONFIRMED by reading the full path (Leave button -> `do_leave` -> no squadron cleanup; `tick` and `h_FireTool` both gated on the connection). Not verified at runtime: how long the mirrors survive, how the game's AI treats a political id from another save, and whether such ships can be written into a save. CLAUDE.md section 2 says our objects must never go into a save, so this needs a check.

## Fix
Already fixed by BUG-0033 (a387323, merged in main before wave 2): `end_session_modules()` runs `squadron::on_session_end()` for `do_leave` too, so the guest's mirrors are destroyed and the maps cleared on Leave. Checked on main 05ee534 (2026-10-28, fix/w2-space): `do_leave` -> `end_session_modules(...)` -> `squadron::on_session_end()`. Not changed further.

Suggested in the review:

Call `squadron::on_session_end()` in `do_leave`, before `g_remotes` is cleared. A shared session-end helper for `do_leave` and the disconnected callback is filed separately by the core review; this issue is about the Space part. Keep the old path behind `coop.fix leavefleet off` for the lab.

## Verification
Covered by `coop_leave_cleanup.py` (suite `leave_cleanup`, BUG-0033): Space fixture, the guest presses Leave and the host's mirror ships must be gone from its game; FAILS with `coop.sessionfix 0`, passes with the fix (runs in BUG-0033). Re-run on fix/w2-space in lab run out/lab/20261027-233002 on fix/w2-space (23/23 with smoke).

Not done yet. Planned test (must FAIL without the fix and PASS with it):

`tools/tests/coop_space_leave.py` on `twoinst.start('space')`:
1. Host and guest land on the same planet. The host spawns 3 pirates (`coop.fleet spawn 2`).
2. Wait until the guest's UFO noun list (the game's `GetAllUFOs`) holds the 3 mirrors.
3. The guest runs `coop.leave`.
4. After 2 s, assert that the guest's UFO noun list holds no ship with the host's political id or the mirrors' pointers, and that the guest's ship HP has not dropped over 10 s.
5. Fails on main (the 3 ships stay listed), passes with the fix. No crash report on either game.

### Related
- Same gap for the Cell stage, already in docs/known-issues.md ("Cell: a guest's own coop.leave keeps its mirrors as passive cells"). The general session-end helper is filed separately by the core review.
- docs/re/space.md sections 1, 9.4 and 10.2.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #23.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: no longer applies on main: fixed by BUG-0033 (a387323, end_session_modules on Leave); test leave_cleanup (fix/w2-space).
- 2026-10-28: released in 0.7.3.0.
