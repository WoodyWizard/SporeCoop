---
id: BUG-9138
title: Tribe: a partner who plays the host's species stops the founding of every partner after them, without a message
status: open
kind: bug
stage: Tribe
reported: 2026-11-12
source: code review during roadmap phase 4 (BUG-9135)
role: host
versions: 
fixed_in:
branch: phase4-tribe
commits:
tests: tests/tribe_late_tests.cpp (unit, the pick of the partner to found)
signature: host log has `tribe automatic founders: village for player N` for no partner after the one who plays the host's species; no reason line for that partner
docs: docs/re/tribe.md#tribe-late-joiners
related: BUG-9135 BUG-9137
switch: tribe.founding_skip
---

## Report
Found reading tribe.cpp found_partners during roadmap phase 4 (joining late). With three or more players, a partner who
plays the host's tribe species keeps every partner who joined after them from getting a village, and nobody is told.

## Root cause
Inferred from the code (not seen in a game yet). found_partners walks the unseated partners in join order and founds
the first one founding_ready accepts. founding_ready only refused a partner with no creature, one whose creature was
not settled, or one who plays one of the host's own creations (creations::plays_host_creation: a key from the host's
My Creations sent without records). A partner on a founder's species passed when that species is not one of the
host's My Creations (a premade or Sporepedia creature, sent with its records), or when it is an outfit or another
partner's founder. found_partners then stopped at that partner (`key.instance == home_species`: `stop({})` without a
message; another founder's species: "plays the creature of another founder's village") and returned, every 5 s, so
the partners after them in join order were never reached.

## Fix
founding_ready asks tribe_late::founding_wait (tribe_late.h): a partner whose creature is a founder's species (the
host's village species, a seat's founder or one of its outfits) waits with a reason (FounderSpecies: told to
everybody, with tribe.late_prompt the words of BUG-9135 phase 4) and found_partners founds the next ready partner
(tribe_late::pick_partner). `coop.fix tribe.founding_skip off`: as before. Branch phase4-tribe (not merged).

## Verification
Unit: tests/tribe_late_tests.cpp "fixed: the second partner is founded" and "switch off: the blocked partner is picked
first (the bug)". Live (to do, three games): host on Tribe, partner 1 joins and plays the host's species
(`coop.creations choose creature <the host's species key>` when it is not in the host's My Creations, or an outfit
key), partner 2 joins with a creature of their own. Switch off: no village for partner 2 (FAIL run); on: partner 2
gets a village, partner 1 gets the notice and waits (PASS run).

## History
- 2026-11-12: opened.
- 2026-11-12: fix built on phase4-tribe (tribe.founding_skip), unit-tested; live check pending.
