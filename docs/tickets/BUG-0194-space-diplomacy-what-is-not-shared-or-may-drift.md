---
id: BUG-0194
title: Space diplomacy: what is not shared or may drift (space_diplomacy.cpp)
status: open
kind: feature
stage: Space
reported: 2026-10-27
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: both
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0544
---

## Report
A list of limits and open ends of a feature, kept as one ticket.

Copied from docs/known-issues.md, where the entry stays up to date:

* **Names and species of NPC empires differ between the copies**: each copy picks them at the first
  contact (from that player's creations). The relation, contact and war/alliance state are shared
  by political id; notices name both ("Kirgle Empire (Alice: Judge Empire)"). Sharing names would
  need the species too (docs/re/space.md 12.3).
* Events the game raises on every copy by itself are not sent (personality / archetype at contact,
  the empire AI's be nice / be nasty, embassies, super power, avoided contact). Any other event
  between the player and an NPC empire is sent, also when the local AI caused it (for example an
  NPC destroying the player's buildings): if both copies' AI do the same, the host counts it twice.
  Guests converge on the host's table within 20 s; the host's own copy is the reference.
* An empire created after the join (uplift of a civilization, ...) gets a political id on each copy
  by itself (`NextPoliticalID`), the ids may differ: its events are dropped as "unknown empire"
  (`coop.space diplomacy` counter `unknown`). Not seen in tests.
* The comm screen is not driven by clicks in the test: `coop_space_diplomacy.py` calls the same
  game functions its buttons do (ApplyRelationship with the gift event, DeclareAlliance,
  DeclareWar, DeclarePeace); the money of a gift goes through the shared budget. Attacks on an
  empire's ships, missions and trade use the same hook but are not played in the test.
* Rivals mode (feature 10) turns sharing off; it is not tested here (the switch belongs to
  space_rivalry.cpp).

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-11-04 (fix/w42-space-play): found and fixed a drift players meet: a guest's diplomacy while the host is
  in an editor from Space was lost on both copies (BUG-0544, diplomacy.keep_away, test space_diplo_away). Checked
  without a change: play_space.py on the `space` fixture makes no diplomacy at all (host table "0 empires" all run
  long, lab 20261103-012624), so the "both copies' AI raise the same event" double count was not seen; a guest's hit
  on a mirror of a host NPC ship is not applied locally (squadron.cpp returns 0), and the mirror's death goes
  through TakeDamage with pol -1 and no attacker, so that path does not count twice. Still open from this list:
  names and species per copy; the possible double count of AI events on the host (not seen); empires born after
  the join; the comm screen is not clicked; rivals mode not tested here. Also noted: with 3+ players the host
  relays a guest's change before applying it, so a host table sent in between could undo it on the third player
  for up to 20 s (the host now sends no table while partners' changes wait; not tested with 3 players).
