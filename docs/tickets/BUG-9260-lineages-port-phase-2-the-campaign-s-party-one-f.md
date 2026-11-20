---
id: BUG-9260
title: Lineages port, phase 2: the campaign's party, one founder seat per player, kept next to the planet
status: open
kind: feature
stage: All
reported: 2026-11-20
source: PR #196 (Lineages): port onto 0.7.15.2
role: host
versions: 0.7.15.2
fixed_in:
branch: feat/lineages-party (fork IltonBJSilva/SporeCoop)
commits:
tests: tools/tests/coop_party.py (suite party)
signature:
docs: docs/01-architecture.md#architecture-party
related: BUG-9258 BUG-9259
---

## Report
The owner wants every player to have their own creature, nest, tribe and city in the same world, allied (co-op), and
to win Civilization together when only the players' nations are left (2026-11-20). Lineages (PR #196) does this with
founder seats: every stage indexes "whose is this" by the seat of the campaign's party. Phase 2 of the port brings the
party itself.

## Root cause
Not a bug: nothing in 0.7.15.2 remembers which players play a planet; the co-op roles are by network id and end with
the session.

## Fix
party.{h,cpp} and the party part of founders.{h,cpp} from Lineages: the host seats the connected players by their
identity (BUG-9258), keeps each seat's species and name, and saves `<planet>.spo.duoparty` next to the planet (outside
the frame, through a temporary file; only grows; an identity is never replaced). `coop.party` prints it. A lifecycle
hook drops it on a world change or the session end. No wire change (host only).

## Verification
`make test` passes (SKIP_SANITIZERS=1, WSL Ubuntu 24.04).
Run in the game on 2026-11-20, Windows 11, Steam build (`3010a413...`), two instances on one PC (profiles dev1 / dev2),
installed by SporeCoopSetup.exe into a clean copy, the steps of tools/tests/coop_party.py over the debug console (the
lab runner is Linux only), on a Civilization planet without a party file:
* the guest takes seat 1 under its identity (07BDCA5A), the host is in seat 0 (1E9EAA7B), two seats, the party is
  written once next to the planet (`Kealeneon.spo.duoparty`, 40 bytes); the guest keeps no party
* the guest leaves: seat 1 stays (`player=offline`); it joins again: seat 1 again, still two seats
* the host goes to the galaxy menu (no party there) and loads the planet again: the guest is in seat 1 again, read
  back from the file; no faults on either game
* `species=00000000` on a Civilization planet: no creature avatar there; a seat's species is filled in Creature.
Found on the way and fixed before the commit: in the galaxy menu world::current_game_key() still names the planet
played last, so a player joining the host in the menu was seated on it (the party now ends in the menu, game mode
kMenu). The first run of the test also passed on a planet that already had the party file (its "saved" check counted
writes): the test now asks for a connected seat and for the write of a fresh fixture.
no-fail-run: a new feature (on main `coop.party` is an unknown command); the lab runs are still to do on the Linux lab.

## History
- 2026-11-20: opened; phase 2 ported on feat/lineages-party, tested in the game on two instances (Windows, Steam).
