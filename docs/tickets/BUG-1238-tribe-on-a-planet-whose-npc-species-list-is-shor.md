---
id: BUG-1238
title: Tribe: on a planet whose NPC species list is shorter than the tribes it allows (4 species, 5 NPC tribes) the last village never forms and the goal bar stays at 4 of 5
status: open
kind: bug
stage: Tribe
reported: 2026-11-18
source: BUG-1233 analysis (final check 20261117-154911)
role: host
versions: 0.7.14.1, unreleased
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-form-species, docs/known-issues.md#issue-tribe-species-list-short
related:
---

## Report
Found while analysing BUG-1233 (final check 20261117-154911, campaign_tribe_noclick, 4 players). The campaign's own
planet (Scalinus, made by the stage wizard; its Tribe segment save out/lane3/campaign/20261117-154914/segments/tribe.tgz)
has a property list of 10 for the NPC tribe controller (up to 5 NPC tribes, 6 with the player's) but a species list of
only 4. The controller forms 4 NPC villages, then the fifth needs list[4], past the list: before BUG-1233 that read
faulted every second, with `tribe.form_species` the call is held. Either way no fifth village forms: the campaign sat
at "no village left to raid (goal bar (4.0, 5.0))" from 16:21 until the host crashed at 16:26, and with the fix it
would sit there for good unless the stage can be finished another way.

## Root cause
Observed: species list 4 / property list 10 on that planet after loading its save (lab 20261118-002106); the tribe
fixture (Zorispe) has 5 / 10. The list is built at the stage start with one species per archetype pair only when the
species manager has a species of that archetype (0xCC6160), or from the saved keys on a load (0xCC7140)
(docs/re/tribe.md#tribe-form-species).
Not known: which archetype had no species (the test profile's Sporepedia, the new planet from the Creature stage) or
whether the save kept only 4 keys; whether the single-player game ends the same way on such a planet (the read past
the list is the game's own, so most likely yes: a fault every second and no village); whether the goal bar can still
be filled there (an alliance per village counts like a conquest, but only 4 villages exist).

## Fix
Not fixed (not part of BUG-1233). Options: let FormTribe take the list's last species when it is used up (the call with
a list one longer for that call), or fill the list from another archetype's species.

## Verification
no-fail-run: open, not fixed yet.

## History
- 2026-11-18: opened from the BUG-1233 analysis (fix/w52-formtribe).
