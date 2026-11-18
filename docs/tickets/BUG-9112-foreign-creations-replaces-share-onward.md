---
id: BUG-9112
title: Foreign Creations (the receiver's choice) replaces Share Onward (the author's choice)
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: player decision (43-dev)
role: all
versions: 0.7.3.6-lineages42-dev
fixed_in: 0.7.3.6-lineages43-dev
branch: feat/lineages-43
commits: dc6cd5ae
tests: tests/library_sync_tests.cpp (foreign creations only to the guest that wants them); QA host + local guest, see Verification
signature: -
docs: docs/re/creations.md#creations-foreign-creations
related: BUG-9105 BUG-9111
---

## Request

"Foreign Creations: On / Off. When on, you receive all creatures in all connected players' Lineages
Cache and they become eligible to spawn in your worlds. This allows the player the choice to receive
creations that aren't the connected player's." With Share Onward (BUG-9105) the author decided; a
player had no way to refuse a partner's whole library either.

## Implementation (mods/coop/library.cpp, net/library_sync)

- Everyone always gets the connected players' own creations (unchanged).
- `foreign_creations` (coop.ini [library], default off; `coop.library foreign on|off`; lobby button
  "Foreign Creations: On/Off" in Share Onward's place, tooltip "Also get creations your partners
  received"): also everything else in the connected players' Caches.
- The exchange runs through the host: `Exchange::eligible_to(peer, item)` lets the host offer each peer
  by its choice (a connected player's own creation to everyone, any other only to peers that want
  them); a guest offers its received creations to the host only when the host wants them, and a host
  that does not keeps no guest's foreign creation (`foreign_refused`). Switching on re-offers at once.
- MSG_LIBRARY kind 5 keeps its layout (u8 5, str8 source identity, u8 flag); the flag now means "I want
  foreign creations". relay.bin (Share Onward's opted-in authors) is no longer read.
- Player Cells (BUG-9111) in a host's world: its own cells, the connected players', and with the
  host's Foreign Creations on, every other cell of its Cache.

## Verification

- tests/library_sync_tests.cpp: four seats, the host offers a foreign creation with `eligible_to`
  allowing only guest 2: guest 2 gets it, guests 1 and 3 do not (PASS, Windows build of the test).
- QA (host LineagesCellQA, guest LineagesDiff41Guest over 127.0.0.1): the host imported a creation
  of a third source ('Lilku', `coop.library import`). Guest with Foreign Creations off: not received
  after 60 s. `coop.library foreign on` on the guest: the host saw `peers_foreign=1`, the guest received
  and imported Lilku (imported 10 -> 11). Host off: `foreign_refused=0` (the guest offered none). No
  [ERR] on either game. Lobby buttons fit (host Off, guest On).
no-fail-run: a new rule; the exchange test asserts the per-peer filter.

## History
- 2026-11-10: built on feat/lineages-43 (supersedes BUG-9105's Share Onward).
