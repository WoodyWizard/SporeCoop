---
id: BUG-9244
title: Creature: a species' goal card says it is extinct while its creatures still live (the line shown twice)
status: fixed
kind: bug
stage: creature
reported: 2026-11-16
source: owner 2026-11-16 (62-dev QA)
role: host, guest
versions: 0.7.14.1-lineages62-dev
fixed_in: 0.7.14.1-lineages63-dev
branch: feat/lineages-47
commits:
tests: manual Windows QA (TITAN QA host, Dorger.spo copy: coop.social species with the fix off / on)
signature:
docs:
related: BUG-9151, BUG-9182, BUG-0460
---

## Report
Owner 2026-11-16 (62-dev, a QA game on TITAN, screenshot): the Hunt card says "The Crikiitis species is extinct!" twice while Crikiitis stand next to the player. Seen on the QA guest Titan too ("The Bye Mouth species is extinct!", out/shots/r66/fill_2.png).

## Root cause
Observed 2026-11-16 (TITAN QA host, the Dorger.spo copy, `coop.social species`, `coop.nest herds`, `coop.social cards`, screenshots out/shots/r66/card_065D.png): next to a living Bye Mouth (065D0889, its nest at (476 61 -154) with respawn 120000 ms, the value the mod leaves on the nests it makes for players) the Hunt goal read "The Bye Mouth species is extinct!". Its record: relation 6, impressed 0, killed 0. Twelve species had that record; the party's four seats (29232BF0, 7C068C08, 7CE63A2C, 067C5579) and eight others that no seat plays now (06538C27, 7D432D81, 065D0889, 7CD8B40C, 0654D6B6, 7C64D9C7, 066890BD, 06725798).
home_nests (decision 7) gives every party species relation 6 (social::set_party_ally: best friends), at a seat's nest and whenever the nest follows its player's species (BUG-9151). Relation 6 is the game's own "goal done": an Impress goal reads it as allied, a Hunt goal as extinct (the game's extinction 0xBA4750 writes 6 too, docs/re/creature.md 8.1 and #creature-relations). A seat's species changes often (the test host's log: seat 1 went 0654D6B6 -> 066890BD -> 06538C27 -> 7C068C08 in one day, a guest first reporting another species before its own), and nothing took the 6 away from the old one: every species a player ever had stayed "done", and a Hunt goal on one of them said extinct while its creatures lived. The Hunt goal keeps its text once done (setting the record back did not change the panel at once; inferred: the next goal the game picks replaces it; not seen).
Not checked: the owner's Crikiitis (no name lookup for keys); by the same pattern an old seat species. Open: a CURRENT party species (relation 6) as a Hunt goal would read "extinct" too (not seen: the current seats' nests had no members in that world).

## Fix
- social.stale_party_marks (social.cpp clear_stale_marks, host, every 3 s in Creature once the party roster is known): a record with relation 6, no impressions and no kills, whose species is no party seat's now and not the host's own, is set back to 0 (not set: the archetype decides, as before it was marked); logged per species. The game never leaves 6 without impressions or kills on any other species (an alliance needs impressions, an extinction kills), so real outcomes stay. The guests follow the host's table. `coop.fix social.stale_party_marks off`: the marks stay (62-dev).

## Verification
FAIL off / PASS on (TITAN QA host instance 9, Dorger.spo copy, hosting, 2026-11-16): off: 12 species with relation 6 / 0 / 0; on (switched in the same session): 4 left (exactly the four seats' species), the 8 others set back to 0 within 3 s, one log line each (065D0889 Bye Mouth among them). Not verified: the Hunt panel text changing (it keeps a finished goal's text; see Root cause).

## History
- 2026-11-16: opened from the owner's screenshot.
- 2026-11-16: cause found (stale party marks, relation 6 = "done"); fixed (social.stale_party_marks); FAIL off / PASS on on the table.
- 2026-11-16: released in 0.7.14.1-lineages63-dev.
