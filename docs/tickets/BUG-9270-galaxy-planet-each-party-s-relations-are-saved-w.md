---
id: BUG-9270
title: Galaxy planet: each party's relations are saved with the planet and survive a new session or a host of another party
status: fixed
kind: feature
stage: creature
reported: 2026-11-19
source: owner decision 6W (continue: C)
role: host
versions: 0.7.14.1-lineages64-dev
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 9b0c3823
tests: manual on TITAN QA 8 + 9 (three sessions: save, rejoin, the other party hosting)
signature: "party relations file: saves N, loads N, host swaps N" in coop.social species (host)
docs: docs/12-server-and-players.md#server-party-relations-saved
related: BUG-9267, BUG-9269
---

## Report
Decision 6W C (continued on the owner's "Just continue as far as you can"): the parties' relations (BUG-9267) lived in
the host's memory: a new session, a restart or a hand-over to a player of another party lost every other party's
records, and a new host of another party played by the old host's party's table.

## Root cause
By design of BUG-9267's first step (in memory only).

## Fix
social.cpp: after every native save the host writes `<save>.spo.duo-relations` next to the planet (slot folder: it
travels to the galaxy server, to joiners and to hand-overs with the world). Each player identity keeps its party's
records (the game's table for the host's party), and the file names the identities whose party's records are the
game's own table. On the first Creature tick of a planet the host reads it: a party gets its records back from any
of its members (party_table); a host of another party than the file's owners gets its own records written into the
game's table (relation 6 records stay: the players' species, extinctions, BUG-9244), and the old host's party keeps
the game's records as its own. Under `coop.fix social.party_relations`.

## Verification
no-fail-run: a new feature (before it the records were in memory only and gone with the session). Manual, TITAN,
Samuth on the test galaxy, species 06539202:
* session 1: QA 8 hosts, QA 9 (another party) kills 3 (`coop.social pevent 52 0 1`): the host's record killed 0, QA
  9's party's 3 (relation 1); `coop.galaxy save`: samuth.spo.duo-relations (2056 bytes) in the host's slot, "saves 2".
* session 2 (both games restarted, same roles): "loads 1"; QA 9's party (a new party id) has relation 1, killed 3
  again and its game shows it; the host's record still 0.
* session 3 (restarted, QA 9 hosts the stored world, QA 8 joins): "host swaps 1": QA 9's game table relation 1,
  killed 3 (its own), QA 8 (the old owner, now a guest) sees relation 0, killed 0 (its own).

## History
- 2026-11-19: opened and fixed.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
