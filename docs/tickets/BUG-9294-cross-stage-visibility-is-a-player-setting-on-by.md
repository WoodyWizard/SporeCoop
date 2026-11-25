---
id: BUG-9294
title: Cross-stage visibility is a player setting, on by default (decision 7I A)
status: fixed
kind: feature
stage: any
reported: 2026-11-20
source: decision 7I A (docs/ROADMAP.md)
role: any
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 4dd70c23
tests: tools/tests/galaxy_server_check.py (BUG-9294 lines); in game: TITAN QA runs
signature:
docs: docs/12-server-and-players.md#server-era-visitors-solid
related: BUG-9287, BUG-9288
---

## Report
Decision 7I A (the owner, 2026-11-20): "Maybe we should have cross-stage visibility be optional. When disabled,
players in other stages won't see them and they won't see the players in other stages." On by default.

## Root cause
Feature.

## Fix
net/galaxy.h ERAS = 36 (player -> server: u8 on), sent on each WELCOME and change. The server keeps it per player:
off, its VISIT and VISITOBJ go to nobody and it gets nobody's. The game: coop.ini [galaxy] other_eras (default 1), chat
"/eras", "/eras on", "/eras off" (a notice says what changed), `coop.visit eras on|off`; off, it sends no visits and shows
none. Not built: a button in the co-op window (the chat command for now).

## Verification
- tools/tests/galaxy_server_check.py, "other eras hidden by the player (BUG-9294)": FAIL on the server of HEAD
  19e32d1d (2 FAILED: the hidden player still got a VISITOR, and its own reached the other), PASS on the new one (all passed).
- In game (TITAN QA 9 Creature, QA 7 Tribe, QA 8 Civ on Abber): before, QA 9 visitors=1 props 9, QA 7 visitors=1;
  QA 9 `/eras off`: QA 9 visitors=0 props 0, QA 7 visitors=0, the server's log "player 12: other eras hidden"; `/eras on`:
  both back (visitors=1, props 9). The notice was cut at two lines, so it was made shorter (not seen again after that).

## History
- 2026-11-20: opened.
- 2026-11-20: built; server check FAIL/PASS, seen in game.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
