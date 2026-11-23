---
id: BUG-1434
title: After a role swap the new host plays from its co-op copy of the old host's galaxy (an old planet starts instead of the session's world), and the copy keeps the old host's marker, so the next share replaces or moves the new host's progress
status: open
kind: bug
stage: Creature
reported: 2026-11-22
source: discord
role: all
versions: 0.7.15.5
fixed_in:
branch: fix/w64-share, fix/w75-share
commits: 2d3bc204, f6f56ea1, 6fba22c4, COMMITS_W75
tests: copy_slot_keep, copy_slot_notice, copy_slot_planets (coop_copy_slot.py); twins copy_slot_keep_off, copy_slot_notice_off, copy_slot_planets_off, copy_slot_continue_off; win_unit test_slot_own_progress (--old-own); core_tests share_one_planet
signature:
docs: docs/03-gameplay.md, docs/06-testing.md#testing-share-menu-pass, docs/06-testing.md#testing-share-one-planet, docs/re/space.md#space-share-one-planet, docs/04-netcode.md#netcode-reserved-0717, README.md, docs/INSTALL.md
related: BUG-0087, BUG-0035, BUG-0254, BUG-0676, BUG-1435, BUG-1436
switch: world.copy_own_save, world.copy_notice, world.share_planet, world.continue_coop
---

## Report
Two players, Creature. After the first host left, the players swapped roles. The new host's game started an older planet of the first host's galaxy instead of the session's world (its Cell progress at 100 %). Later the new host's save of that game was missing. Full analysis: out/research/dc-report-20261122-world-switch.md in the main checkout (not in git; six SporeCoop-report zips, coop 0.7.15.5, Steam).

## Root cause
Confirmed (code + logs): a guest gets a copy of the host's whole galaxy, old planets too (world.cpp:241-250, world.cpp:1765-1773, slotfs.cpp:231-290); after the swap the new host's galaxy menu showed that copy, and nothing warned it (only slotfs reads the slot marker sporecoop.txt). Likely: the planet started was one of the first host's old planets in that copy. Inferred from code, not seen in logs: the new host's saves go into a slot whose marker still names the old host's planet, so the next share of that planet replaces the slot (slotfs.cpp:529-541, rmtree) or tidies it into Games-sporecoop-backup (slotfs.cpp:292-311). slotfs.cpp is unchanged from v0.7.15.5 to rel/0.7.16.

## Fix
0.7.16.1, the safety part (the full fix, sharing only the played planet, is 0.7.17).
(a) A co-op copy with the player's own progress is the player's: `slotfs::own_progress` = an `own_save <date> <planet>` line in its sporecoop.txt, or (slots of older versions) a planet other than the marker's saved after the marker (a guest only plays the marker's planet in a copy). `world::slot_tick` (once a second, every role) adds the line at the first save in the copy the save database points to while this game is not a guest of a session (alone, or hosting from it). pick never reuses such a slot (not even as the last resort), tidy leaves it in Games, and a write straight into it (a guard) moves the old copy to Games-sporecoop-backup instead of deleting it. Chosen over "move it to the backup at the next share": the backup hides the game from the galaxy menu where the player plays it, and a plain mtime rule would back up a 20 MB copy at almost every share (a guest's game saves its copy each time it leaves it for the next world: the report's loop wrote ~170 MB); the own mark keeps the slot where it is and is never touched by shares (only the existing auto_load_world=0 planet backup can move it, never delete). Switch `coop.fix world.copy_own_save off`: 0.7.16.
(b) A host whose save database points to a co-op copy of another player's world (galaxy menu or stage) is told once per copy and planet in a session (co-op window + notification): whose copy it is, the planet it was made for, and in a stage whether the planet played is that one or one of the other player's older games. Switch `coop.fix world.copy_notice off`. No message changed.
Not covered: a save made as a guest and the game quit within 1 s before the watch saw it (the next own save marks it); old slots whose own progress is on the marker's planet itself.

0.7.17, the full fix (WP-W1, fix/w75-share, docs/plan-0.7.17.md item 8):
(c) A share sends the played planet's `.spo` and the galaxy's own files only (stars.db, planetRecords, PlanetScripts,
GGEUserData.dat, lastSave.pld, snapshot.spo, the markers): `share_logic::keep_played` drops the slot's other
`<name>.spo` files right after the slot is read (world.cpp share_now), so the guest's copy holds no old planets of the
host. Only for a guest whose world family is 1.1 or newer (`peer_families` on the host, `share_logic::peer_takes_one`;
a 0.7.16.1 guest gets the whole slot as before) and only when the played planet's file is in the list (a share from a
menu sends the slot as before). GGEUserData.dat is not rewritten: the galaxy's files still list the old planets, the
galaxy menu marks a missing save and its Play step says "Bad or Missing Saved Game" instead of loading it
(docs/re/space.md#space-share-one-planet). Switch `coop.fix world.share_planet off`: the whole slot (0.7.16.1).
(d) "Continue the last co-op world": a host in the galaxy menu without a game gets "Continue <planet>" on the co-op
window's greyed-out "Send my world" when a co-op copy holds the planet it was made for (the copy the save database
points to, else the copy whose planet file was written last); a click loads it as the player's own load (coop.load), and
the guests that join get it as usual. `coop.continue [go]` is the console twin. The menu notice says when a copy holds no
old planets and points at Continue. Switch `coop.fix world.continue_coop off`: no button (0.7.16.1).
Not done: rewriting GGEUserData.dat (its record format is the game's own; not needed while the missing saves cannot be
started). Space: a share of a Space game also leaves the other planets' saves out; the galaxy's files (planetRecords,
stars.db) carry the Space data, so nothing of the played game is lost (to be watched in the Space share tests).

## Verification
Open, waiting for the lab run. PASS expected: copy_slot_keep (the guest saves alone in its copy S, rejoins, the host shares twice: S's planet file keeps its bytes, the world goes into another slot), copy_slot_notice (the host leaves, the guest hosts from S: a notification says it hosts the planet from its copy of Alice's world, made for that planet), win_unit (test_slot_own_progress). FAIL expected: copy_slot_keep_off (SPORE_FIX_OFF=world.copy_own_save: the second share replaces S), copy_slot_notice_off (SPORE_FIX_OFF=world.copy_notice: no notification), `win_unit.py --old-own` (SPORE_FIX_OFF=world.copy_own_save).

0.7.17 part (fix/w75-share): open, waiting for the lab run. pending night run: copy_slot_planets, copy_slot_planets_off,
copy_slot_continue_off (one lane each, creature fixture, ~8 min). PASS expected: copy_slot_planets (the guest's copy holds
the played planet and the galaxy's files, not Oldworld.spo / Ancient.spo; its galaxy menu lists the planet with its save;
after the role swap "Continue <planet>" loads it on the new host and the old host joins into it). FAIL expected:
copy_slot_planets_off (SPORE_FIX_OFF=world.share_planet: the copy holds all three planets), copy_slot_continue_off
(SPORE_FIX_OFF=world.continue_coop: no Continue button). core_tests share_one_planet (make test) covers the file rule.

## History
- 2026-11-22: opened.
- 2026-11-22: safety part fixed on fix/w64-share (own progress in a copy is kept, the host is told whose copy it plays), tests and twins, waiting for the lab run; sharing only the played planet stays for 0.7.17.
- 2026-11-23: the full fix on fix/w75-share (0.7.17 WP-W1): a share sends only the played planet and the galaxy's files (world 1.1 guests), Continue for a new host in the galaxy menu; tests and twins written, waiting for the night run. Idea of the safety part and the report triage as before; nothing ported from outside code.
