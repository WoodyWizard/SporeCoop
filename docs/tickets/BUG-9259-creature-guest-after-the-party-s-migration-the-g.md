---
id: BUG-9259
title: Creature guest: after the party's migration the guest's home nest flips back to the old nest (no nest, no mating, repeated 'Bringing your nest')
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: player report (SporeCoop-report-20261118-215146.zip), owner
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: c3ba2fbe
tests: tests/win_tests.cpp test_party (a handed-over seat); manual MSI pair
signature: "nest: our home nest ... has no members here yet: joined later" every 10 s; "not archived"
docs:
related: BUG-9128, BUG-0640, BUG-9151, BUG-9175, BUG-9210, BUG-9163
---

## Report
Owner 2026-11-18: BigTexas "is also unable to see their own nest, and they cannot mate", and "has been repeatedly
getting Bringing you to your nest notifications and being forced into loading."
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Observed in the report (guest log) and the host's log, not reproduced yet. Host 22:42:48: "seat 1 (BigTexas)'s nest at
(-257 431 -28) follows its evolution", then "BigTexas's species is migrating to a new nest. Everyone migrates
together". Guest: 21:42:47 "our home nest follows the host's (-257 431 -28)", 21:42:47 "wsync: the host's species
migrates ... this game migrates to (-295 406 -67) too (BUG-0640)", 21:43:08 joined (-295 406 -67); from 21:43:18 every
10 s for 10+ minutes "nest: our home nest (-257 431 -28) has no members here yet: joined later" (nest.cpp follow_home,
BUG-9128). Host 22:45 and 22:49: "seat 1's nest has no living member: the next one in 2 s (BUG-9175)".
Inferred: two rules pull the guest's home two ways: the migration follow (BUG-0640) moved it to the new nest, while the
host's home message (MSG_NEST kHome, the guest's own seat nest of home_nests) still names the old one, whose herd is
empty here. The "Bringing your nest" loads (world shares of BUG-9167 / BUG-9210) were not found in this log (the player
restarted the game six times in 30 minutes; the earlier logs hold some).

Found on the MSI pair 2026-11-18 while reproducing (owner: "They never got a nest to migrate to ... We should check if
the player loading in has a creature and a nest at the bare minimum"), five causes, all observed:
1. creations.cpp decode_founder refused every saved custom creature's archive ("Unexpected record"): since BUG-0921 a
   bundle carries the creation's Sporepedia pictures (PNG records). The host never had the player's species, so the
   player's seat nest was never made (home_nests waited silently for the species profile).
2. party.cpp: with all four play seats taken by players who are not online, a new player got no seat (no nest of its
   own); and the party save then refused a seat that changed hands ("A party seat belongs to another player").
3. creations.cpp choice_here (BUG-0638): a fresh guest's first creature (made in the 2,000 DNA pregame editor) was not
   in the host's world yet, so the guest was handed the host's creature and sent to the editor again (the owner's
   "it gave me his creature and had me change it").
4. home_nests.cpp: a player's nest with no living member waited for the game's own respawn, which did not come for
   half an hour in the report (no family: no mate, no nest to see).
5. wsync.cpp: the migration follow (BUG-0640) and the host's home message pulled the guest's home two ways (above).
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
Commit c3ba2fbe, each part behind its switch:
1. `creations.founder_png`: the archive takes up to 8 pictures (1 MB each at most) and skips them.
2. `party.reuse_offline_seat`: a newcomer takes the last seat (not the host's) whose player is not online; the party
   save accepts that seat's new owner (founders::record_party handed_over).
3. `creations.first_creature_here`: a guest's first creature that the host's world does not have yet stays its own.
4. `creature.nest_family_fill`: after 15 s with no living member the host puts 3 members of the player's species in
   its nest (cCreatureAnimal::Create into the nest herd, as the respawn does), at most every 30 s. A seat whose species
   the host does not know logs a warning once a minute.
5. `wsync.nest_told_home`: a guest follows a migration only to the home nest the host named in the last 5 s.

## Verification
tests/win_tests.cpp test_party: a handed-over seat (FAIL before the founders change: "A party seat belongs to another
player"; PASS with it).
no-fail-run for the rest (two PCs, the game's world): manual playthrough 2026-11-18 on the MSI pair (host Samuth, a
fresh guest profile, 65-dev plus these fixes): the fresh guest's first creature kept ("plays its own creature", no
second editor); the guest took seat 3 of a player not online and the party saved (saves=1 errors=0) and held after a
reload; the archive was written with its pictures (50 833 bytes, no "not archived"); the seat nest followed the guest's
species with members; the guest's home followed the host's message; the guest died, its death cinematic showed its own
nest and family and it came back 0.7 m from its nest; mating was offered, the egg scene and the editor (348 DNA, locked
parts) followed. Not seen at runtime: the family fill firing (the game's respawn refilled the nest during the test), a
migration after these fixes, an evolution to a new species.

## History
- 2026-11-18: opened.
- 2026-11-19: five causes found and fixed on feat/lineages-47 (c3ba2fbe).
