---
id: BUG-9204
title: Creature: partners are missing from each other's minimap (the host sees no guest, guests see the host but not each other)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-14
source: owner
role: 
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: a8a3ceca
tests: tools/qa_win (TITAN, host 9 + guest 8 over 127.0.0.1, 2026-11-14)
signature:
docs: docs/re/creature.md#creature-party-nests-hatchling-size
related: BUG-0561 BUG-0460 BUG-0489
---

## Report
Owner play test 2026-11-14 (3 players, Creature): partners do not see each other on the map. Guests see the host, but not each other; the host sees neither guest. The owner: they should simply be discovered.

## Root cause
Not proven yet. Creature has only the game's own minimap (nests with faces and the discovered mark, docs/re/creature.md around the 0xD425A0 / 0xE10560 notes, plus the own avatar); no mod code puts partners on it (partner tags exist only on the Space galaxy map, space_map.cpp). Hypothesis (low confidence): what guests see is the host's home nest (the save's original player nest), while each seat nest made at runtime (home_nests.cpp) is marked discovered only on the game that owns it. Next: a minimap screenshot on each game to tell nests from creature dots.

## Fix
home_nests.cpp host_tick: every party seat's nest gets nest +0x124 bit 0 (the discovered bit the minimap reads) on the host; the guests get it with the world (the shares carry the save). Switch: `coop.fix creature.party_nests_known off`.

## Verification
no-fail-run: manual on TITAN. Runtime proof first (19:47): `mem.write` of +0x124 = 1 on a seat nest turned its "?" into the friendly face at once, 0 brought the "?" back. FAIL (19:53, switch off, both seat nests' bit cleared): both "?" on the host's minimap. PASS (switch on): `a party nest marked discovered` twice within 2 s, both nests the friendly face; the guest's minimap showed both other party nests as friendly faces.

## History
- 2026-11-14: opened.
- 2026-11-14 (night): fixed (decision 3V A), live on TITAN.
