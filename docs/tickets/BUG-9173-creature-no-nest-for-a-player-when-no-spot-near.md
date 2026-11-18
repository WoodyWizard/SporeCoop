---
id: BUG-9173
title: Creature: no nest for a player when no spot near the host nest is at its height (no mate icon)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: test
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + the MSI as a guest + a TITAN guest, 2026-11-13, Samuth)
signature:
docs: docs/re/creature.md#creature-mate-keeper
related: BUG-9135 BUG-9151 BUG-9167 BUG-9169
switch: creature.nest_site_closest_height creature.nest_note
---

## Report
The owner, on the MSI as a guest (2026-11-13): "The mate doesn't have the icon still." In that session the host made no nest for the MSI player: `coop.homes` "seat 1 ... no nest of its own", failed=42, the log "no flat spot 45-80 m from the host's nest for seat 1" every 2 s; the MSI player lived in the host's nest among the host's species (no mate of its own possible).

## Root cause
Observed: every one of the 60 spots tried (12 directions at 45-80 m) was within 25 m of another nest: a ring of 12 nest herds at 50-65 m round the host's nest, all made by home_nests (target 4, respawn 120000 or 2000 ms, families of 1-3). Each was made for a seat whose species key had changed since its last session: the host registers a partner's creature under a key tied to its network id (roadmap 1b), BUG-9151's follow remembers a seat's nest only within a session, so every such session made one more nest next to the old one.

## Fix
creature.nest_note (on): each seat's nest is noted per planet in worlds.ini (section nest<seat>, key the planet id, "x y z"); a seat with no nest of its current species near the host's home takes back the live nest herd at its noted spot (not the host's, not another seat's) and it follows the new key as after an evolution (home_nests follow_species: herd +0xA4, SetSpecies on the members). creature.nest_site_closest_height (on): the spots tried are followed by 24 directions at 55, 65, 75, 90 and 100 m; with none at the home's height the closest in height (up to 20 m off) is taken; a failure logs why (no ground / height / near a nest).

## Verification
no-fail-run: manual on two PCs (no Windows lab lane). FAIL: `coop.fix creature.nest_site_closest_height off` (07:53): "60 within 25 m of a nest", no nest for seat 1, failed=37. PASS (07:58): "seat 1 (MSIGuest) has its own nest of 7CD76455 at (161 -453 -154), 89 m from the host's", 1 member, the world out 4 s later, the MSI joined it (`coop.nest home`). The note: the host's mod reloaded, a TITAN guest joined first so the MSI came as network id 2: its key 7CD76455 -> 7C5EE376, "seat 1 (MSIGuest)'s nest at (161 -453 -154) follows its evolution 00000001 -> 7C5EE376 (... 3 member(s))" (00000001: found by its note), made=0. FAIL of the note: the 12 nests on Samuth from the sessions before it. Not done: removing those 12 from the test save.

## History
- 2026-11-13: opened.
