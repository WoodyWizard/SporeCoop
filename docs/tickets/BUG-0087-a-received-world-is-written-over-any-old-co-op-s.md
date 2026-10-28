---
id: BUG-0087
title: a received world is written over any old co-op slot and mixed with another host's files
status: fixed
kind: bug
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#65
role: host
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w2-world
commits: df2f0a9
tests: tools/tests/win_unit.py + tests/win_tests.cpp (suite win_unit)
signature:
docs: docs/03-gameplay.md
related: #65
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/65 (full text, discussion).

The comment on `pick_slot` says it reuses "the slot that already holds this host world", and it computes the world id for that (`want`), but never compares it. Any slot with a `host_slot` marker is reused, also one of another host's world, and `finish()` writes the new files on top without removing the old ones. The slot then holds old planets of host A next to host B's `stars.db` and `lastSave.pld`. Severity: wrong behaviour (a broken or confusing galaxy in that slot; loading one of the old planets from the menu reads a star database that does not match it).

### Where
- `want` is formatted and never used; the first loop returns any co-op slot: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L821-L867
- `finish()` writes the received files without clearing the folder: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/world.cpp#L1004-L1024

### How to reproduce
1. Three instances or two sessions: host A shares a world with the guest (the guest stores it in co-op slot GameN).
2. Later the guest joins host B (another galaxy, another planet name). The guest is not playing GameN at that moment (it is in the galaxy menu or its own slot).
3. Look at the guest's `Games\GameN`: it holds host B's files and host A's planet `.spo` from before. In the galaxy menu, that slot shows both planets.
4. Load host A's planet from that slot in the galaxy menu.

### Expected
A new host world goes into the slot of the same host and planet (and replaces its contents entirely), or into a free slot; old files of another world never stay next to it.

## Root cause
**What happens**

1. `pick_slot(world_id, planet)`:
   ```cpp
   char want[16];
   snprintf(want, sizeof want, "%08X", world_id);
   ...
   if (read_file(p + L"\\sporecoop.txt", marker)) {
       std::string m(marker.begin(), marker.end());
       if (m.find("host_slot") != std::string::npos)
           return p; // a previous co-op world: update it
   }
   ```
   The marker also names the host and the planet (`host_slot <id>\nhost <name>\nplanet <name>`), but nothing of it is compared.
2. `finish()` creates the folder and writes each received file. Files of the old world that the new one does not have stay: other planets (`<name>.spo`), files in sub folders, a `snapshot.spo`.
3. The world id is the crc of the whole folder, so it changes with every save. Even for the same host the marker id never matches, so the check would have to use the host or the planet name rather than the crc.
4. Result: after playing with two different hosts (or one host's two galaxies), the reused slot holds a galaxy with planets whose stars are not in its `stars.db`. The galaxy menu shows discs for them; loading one reads a star record that does not exist (docs/re/creature.md 15 saw a load fault in `cStarRecord::GetPlanetRecord` with a mismatched `stars.db`).

### Confidence
CONFIRMED by reading the full path (`pick_slot` and `finish`). PLAUSIBLE effect: what the game does when it loads the leftover planet with the other host's `stars.db` is not verified.

## Fix
Fixed on fix/w2-world (df2f0a9). `slotfs::pick` reuses a co-op slot only when its `sporecoop.txt`
names the same host (player name) and planet; the crc is not used (it changes with every save).
Otherwise a free GameN; only when none of 64 is free, another co-op slot (that is then replaced
entirely). `slotfs::write` writes the world into a fresh folder and swaps it in for the slot as a
whole, so no file of an older world stays next to the new one. Old behaviour:
`coop.worldfix slot_match 0` (and `write_check 0` for the in-place write).

The review suggested:

Match the marker on the host name and planet (write a stable id into `sporecoop.txt`, for example the planet name plus the host's player name, instead of the crc). When reusing a slot, empty it first (or write into a temporary folder and swap it in only after every file was written, which also avoids half-written slots when a write fails). Never touch the busy slot (see the related issue). Old behaviour behind `coop.fix slot_match off`.

## Verification
Windows-side unit test `win_unit` (tests/win_tests.cpp built with llvm-mingw, run under Wine in the lane's bottle by tools/tests/win_unit.py; real Win32 file calls, no game). `win_unit.py --old` switches the fixes off. Lane 3, 2026-10-28: FAIL with `--old` (21 failed checks), PASS with the fixes (lab runs 20261027-225936 and the smoke run of the branch). Case `test_slot_match`: host A's world (Terra), then host B's (Vulcan): with `--old` B's world goes
into A's slot (A's Terra.spo next to B's files); with the fix B gets its own slot, and A's world
coming again replaces A's slot without leaving an old planet file behind.

The planned test was:

`tools/tests/coop_world_slot_reuse.py`: host A shares with the guest, then host B (another fixture galaxy) shares with the same guest. Assert on the guest's file system that each `Games\GameN` with a `host_slot` marker holds exactly the files of one shared world (the received file list), and no `.spo` of the other host. It fails on main and passes with the fix.

### Related
- The issue about the `pick_slot` backup loop moving or writing into the slot being played (same function).
- docs/03-gameplay.md 3.2 (world sharing), docs/re/creature.md 15 (stars.db and planet records must match).

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #65.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w2-world: slots matched on host and planet, replaced as a whole (win_unit).
- 2026-10-28: released in 0.7.3.0.
