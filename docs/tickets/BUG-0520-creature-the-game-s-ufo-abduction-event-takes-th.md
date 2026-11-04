---
id: BUG-0520
title: Creature: the game's UFO abduction event takes the partner's puppet, the host then drives the freed puppet and crashes (dc04aee5 EIP 0x7349626D, coop.dll set_speed+walk_place)
status: duplicate
kind: crash
stage: Creature
reported: 2026-11-03
source: GitHub issue #194 (2026-11-03)
role: host
versions: 0.7.3.6
fixed_in:
branch: fix/w40-gh-issues
commits:
tests:
signature: dc04aee5 EIP 0x7349626D (execute, the string bytes 'mbIs' past a short cModelObject vtable 0x14736D8), coop.dll 0.7.3.6 +0xCA0AC game::set_speed <- +0xD464E drive::walk_place <- +0xD2324 modes::creature_drive <- +0x53890 drive_puppet
docs: docs/re/creature.md#creature-ufo-abduction-event, docs/re/creature.md#creature-puppet-alive
related: BUG-0507, BUG-0486, BUG-0426
---

## Report
GitHub issue #194 (host, 0.7.3.6, GOG 2024, Creature, 2 players over the internet): a UFO came and started to abduct a creature; right after the UFO picked it up the host's game crashed, every time. The guest did not crash and lost the connection (timeout). Attached: the host's crash .txt + .dmp (20261103-204851), two report zips, sporekit.log, coop-last-session.txt. The same PC's earlier dump crash-20261102-163856 (0.7.3.3) has the same EIP.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed (crash .txt, coop.dll built from tag v0.7.3.6 out of tree and symbolized): EIP = 0x7349626D (execute), stack coop.dll+0xCA0AC (game::set_speed, a vcall on c+0xC0) <- +0xD464E drive::walk_place (driver.h:286) <- +0xD2324 modes::creature_drive <- +0x53890 drive_puppet. ebp = 0x1B333068 (the puppet c), ecx = c+0xC0, eax = 0x020036D8 = 0x14736D8 in the exe: exactly the BUG-0507 state (the partner's puppet freed, its memory reused by a cModelObject-family object, slot 0xC4 read past its vtable). The host's log: the partner shown (puppet=shown, d=20 m, 44/44 hp) 3 s before; no hide / despawn line of ours.
Inferred (the player's text, not reproduced): the game's UFO abduction event (cUFOAbductionEvent of the Drama Manager, docs/re/creature.md#creature-ufo-abduction-event) took the guest's puppet on the host and the game freed it; 0.7.3.6 drove the freed pointer (no liveness check in the Creature adapter). The `npc ... updated at the planet's centre ... dead=1` warnings before the crash are not abduction evidence: the lab prints the same line for an NPC 58 s into every Creature run without any UFO.
Not reproduced: a type 7 ship made with the event's own spawn function (UFOAbductionEvent_SpawnUFO) over the puppet and ticked with UFO_TickBehavior each frame, 2 x 60 s, lane 4 (out/lab/20261103-194147 and the runs before it, test code not kept): the ship never got a target (+0x550, +0x7A8, +0x7AC stayed 0) and nothing was taken; the event itself (started by the Drama Manager from prop group 0x0EBCFBC7 in Creature) was not triggered.
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Duplicate of BUG-0507: `creature.puppet_alive` (0.7.4, modes.cpp creature_alive) drops a puppet the game's object list no longer holds as a live creature with its spawn serial, and shows the partner again with a new one, whatever freed it (here, inferred, the UFO). No separate change.
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
no-fail-run: duplicate of BUG-0507 (its FAIL out/lab/20261103-171411 and PASS out/lab/20261103-171523 with the game's own destroy of the puppet); the UFO abduction could not be made in the lab (above).
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-03: opened from GitHub issue #194; same signature as BUG-0507 (duplicate), the UFO event as the inferred trigger; the event's code documented, the abduction not reproduced.
