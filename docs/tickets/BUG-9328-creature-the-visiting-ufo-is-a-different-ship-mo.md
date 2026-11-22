---
id: BUG-9328
title: Creature: the visiting UFO is a different ship model in each game (a guest picks its own, even a test ship of its Lineages Cache)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-21
source: owner report 2026-11-21
role: guest
versions: 0.7.14.1-lineages57-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-ship-purge
commits: bbe18d12
tests: tools/qa_win (manual, two TITAN QA games, 2026-11-21: host coop.drama start 2, coop.drama ship on both)
signature: guest `coop.drama ship`: a ufo model other than the host's last_spawn
docs: docs/re/creature.md#creature-ufo-ship-model
related: BUG-0486 BUG-9131 BUG-0558 BUG-0546 BUG-9329 BUG-9330
switch: creature.same_ship
---

## Report
The owner (2026-11-21): in a Creature playthrough both players had the visiting spaceship event, but they saw different
ships in the sky: one a Grox ship, the other a test ship that was in that player's Lineages Cache.

## Root cause
Observed (Ghidra and runtime, docs/re/creature.md#creature-ufo-ship-model): since BUG-9131 the host relays the UFO
visit (drama.cpp kRelay, kind 2) and each guest's game starts the event itself. The event's start rolls "empire ship"
with the game's random numbers and either takes an empire's ship (the Grox one) or one random ship of that game's own
Sporepedia (UFOAbductionEvent_PickShip, an object template DB query), Lineages Cache imports included. So every game
showed its own ship. The guest's mirror of the host's ship (BUG-0558) is made by the same spawn function and also took
the guest's own pick. A Lineages Cache creation is imported under another key in each game (the same test ship was
2926CA3F on the host, 29215407 on the guest), and the game's default UFO (the zero key) is not in a Creature game.

## Fix
drama.cpp `creature.same_ship`: the host relays the UFO after its own start, with the spawned model key and the Lineages
Cache hash of that creation (MSG_DRAMA: u8 kind, 12 bytes key, str8 hash; older guests read the kind only). On a guest,
UFOAbductionEvent_SpawnUFO and Simulator::SpawnUFO are hooked: every type 7 ship of the event's spawn (the relayed
visit's and the mirror) takes the host's ship, by the hash for a cached creation (library::key_of), by the key
otherwise, when this game has it; else the game's own Grox ship (read as the game does, prop 0x02AE0C7E!0x280852F1),
never a random ship of the guest's own. `coop.fix creature.same_ship off`: the guest's own pick.
Debug: `coop.drama ship`.

## Verification
- FAIL (switch off on both, TITAN QA): host ship FB220279, the guest's two ships 0680E860 and 59FDDB0A (its own picks);
  the shadows differ: out/qa-shots/bug9328_FAIL_off_host_ship.png, bug9328_FAIL_off_guest_ship.png.
- PASS (switch on, final protocol): host ship 2926CA3D (a cached creation), the guest's two ships 29215409, the same
  creation by its hash (guest log: "the visit's ship here is the host's 29215409 ... this game picked 2921540A" and
  "... picked 29215408"); the same shadow on both screens: out/qa-shots/bug9328_PASS_on_host_ship.png,
  bug9328_PASS_on_guest_ship.png.
- Fallback: a host ship the guest did not have (an earlier build that matched by key only) gave the guest the Grox ship
  06576DBD on both of its ships: out/qa-shots/bug9328_grox_fallback_host_ship.png, bug9328_grox_fallback_guest_ship.png.
  The zero key tried first gave an invisible ship (bug9328_zero_key_invisible_guest.png): replaced by the Grox ship.
- No crash report from any of the runs.
- no-fail-run: not a lab run (TITAN QA games by hand); no automated lab test was written or run.

## History
- 2026-11-21: opened.
- 2026-11-21: fixed on fix/l47-ship-purge (bbe18d12), FAIL / PASS live on TITAN's QA copy.
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
