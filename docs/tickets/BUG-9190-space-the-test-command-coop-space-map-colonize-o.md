---
id: BUG-9190
title: Space: the test command coop.space map colonize on the own homeworld crashes the game (GetSpeciesProfile+0x12, read of 0x268)
status: fixed
kind: crash
stage: space
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 3d3d7d9f
tests: tools/qa_win (manual, 2026-11-13)
signature: GetSpeciesProfile+0x12 read 0x268 after coop.space map colonize on a tech-5 planet
docs:
related:
---

## Report
TITAN guest "Late" (2026-11-13 17:46, crash-20261113-174622-i8): my colony test script could not leave the home planet and ran `coop.space map colonize` there; "colony tool fired at 307.5 -334.6 223.8", 3 s later 0xC0000005 at 0x00BFF5D2 Editors::cSpeciesManager::GetSpeciesProfile+0x12 (read of 0x268), the game's main thread. A test command only (a player cannot fire the colony tool on the own homeworld).

## Root cause
Inferred: the test command calls cPlaceColonyToolStrategy::OnHit without the tool's own aiming checks; on a planet that already is an empire colony (tech 5) the game's colony creation reads a species it does not have. Not analysed further.

## Fix
space_map.cpp `map colonize` refuses a planet whose record has tech 5 ("this planet already is a colony"). No switch (a test command).

## Verification
no-fail-run: test-command guard; not run again on a homeworld (it crashes the game). The same command on an uncolonised planet (Athalky) worked afterwards (BUG-9189 run).

## History
- 2026-11-13: opened.
- 2026-11-13: guarded on feat/lineages-47.
