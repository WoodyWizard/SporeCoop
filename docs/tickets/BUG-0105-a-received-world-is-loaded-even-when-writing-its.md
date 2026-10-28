---
id: BUG-0105
title: a received world is loaded even when writing its files failed (disk full, locked or read-only file)
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 2 of main 563f72d, GitHub WoodyWizard/SporeCoop#84
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9
tests: tools/tests/win_unit.py + tests/win_tests.cpp (suite win_unit)
signature:
docs: docs/03-gameplay.md
related: #84
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/84 (full text, discussion).

When a guest receives the host's world, `world::finish` writes every file of the save slot without checking any result. A file that could not be created or was written only partly (disk full, a file locked by an antivirus or backup tool, a read-only file in a reused co-op slot) is skipped silently. The slot is still marked as a co-op world and auto-loaded as if it were complete.

### Where
- Slot folder and sub folders created without checks: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1004-L1013
- Files written, `_wfopen` failure skipped, `fwrite` / `fclose` results ignored: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1014-L1018
- Marker `sporecoop.txt` (`host_slot`) written anyway: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1020-L1024
- The world is then loaded (or loaded after the editor closes): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1039-L1048

### How to reproduce
1. On the guest, fill the drive that holds `AppData\Roaming` (or the test profile's folder) until only a few hundred KB are free. Alternatively, mark a file in an old co-op slot (`Games\GameN` with `sporecoop.txt`) read-only.
2. Join a host whose save is bigger than the free space (or that contains the read-only file's name).
3. The guest writes the slot, logs "world: received N bytes into ...", and auto-loads it. The load hangs or faults. The slot on disk holds truncated or stale files.

### Expected
A world whose files could not all be written completely is not marked and not loaded. The player gets a clear message ("could not write the world: disk full / file in use"), and the partial slot is removed or kept unmarked.

## Root cause
**What happens**

- `CreateDirectoryW` for the slot and its sub folders: no check. If the slot folder cannot be made, every `_wfopen` fails and nothing is written. The game is still told to load the slot.
- `_wfopen` returns null: the file is skipped with no log line and no message to the player.
- `fwrite` writes fewer bytes than the file has (disk full), or `fclose` fails to flush: a truncated `.spo` / `.pkp` / `.db` stays on disk.
- In a reused co-op slot (see #65), a file that cannot be overwritten (read-only attribute, held open by another program) keeps the old host's version. The slot then mixes two worlds, although the checksum of the received data was correct.
- The `host_slot` marker is written and `load_slot` runs. A truncated or mixed save leads to a load that hangs in LoadGame or faults; docs/re/creature.md section 9 and docs/re/space.md section 8 describe broken saves that never finish loading. The player sees "loading the world of <host>..." and then a game that hangs or crashes, and nothing in the log says the write failed.

### Confidence
CONFIRMED by reading the code. A second reviewer checked it independently. Severity low to medium: it needs a full disk or a locked/read-only file, but when it happens the result is a hang or crash with no hint why.

## Fix
Fixed on fix/w2-world (df2f0a9). `slotfs::write` writes every received file into
`Spore\Games-sporecoop-tmp\GameN-<time>` (next to Games, so the galaxy menu and `newest_slot` never
see it), checking `CreateDirectoryW` (`ERROR_ALREADY_EXISTS` accepted), `CreateFileW`, the
`WriteFile` count and `CloseHandle`, and writes the marker last. Only then the old slot is moved
aside and the new folder moved into place (the old one is put back when that fails, then
deleted). On any failure `finish()` logs the path and the error, tells the player ("could not save
the world of X: ... (disk full)"), and neither marks nor loads anything. Old behaviour:
`coop.worldfix write_check 0`.

The review suggested:

- Check `CreateDirectoryW` (accept `ERROR_ALREADY_EXISTS`), `_wfopen`, the `fwrite` count and `fclose` for every file. On the first error, stop, log the path and the error, and tell the player.
- Better: write the received files into a fresh temporary folder next to `Games`, verify every size, then move the folder into place as a whole and only then write `sporecoop.txt`. That also avoids mixing with an older world in the same slot (#65).
- Never call `load_slot` or set `g_deferred` after a failed write.

## Verification
Windows-side unit test `win_unit` (tests/win_tests.cpp built with llvm-mingw, run under Wine in the lane's bottle by tools/tests/win_unit.py; real Win32 file calls, no game). `win_unit.py --old` switches the fixes off. Lane 3, 2026-10-28: FAIL with `--old` (21 failed checks), PASS with the fixes (lab runs 20261027-225936 and the smoke run of the branch). Case `test_slot_write_fails`: a world with a file Windows cannot create: `--old` writes the rest,
the marker and reports success; with the fix the write fails and nothing is marked. A file of the
slot held open by another program: Wine moves the folder anyway (Windows refuses and the write
fails with "a file in it is in use"); either way the slot holds one whole world, never a mix.

The planned test was:

A test for `coop_*` on two instances, with a switch `coop.fix world_write_check off` for the old behaviour:
- Before the join, mark a file in the guest's co-op slot read-only (or point the slot at a full test volume), then let the host share its world.
- Assert on the guest: no `load_slot` / LoadGame is started, the game stays in its menu or stage, a "could not write" message is shown, and no game crash or hang report is written.
- FAILS on main (the guest loads the broken slot and hangs or faults). PASSES with the fix.

### Related
- #65 (a received world is written over any old co-op slot and mixed with another host's files)
- #13 (a received world can move and overwrite the save slot the guest is playing)

## History
- 2026-10-27: found in code review round 2 of main 563f72d, filed as GitHub #84.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: received files written to a temporary folder, checked, swapped in whole; a failed write is not loaded (win_unit).
- 2026-10-28: released in 0.7.3.0.
