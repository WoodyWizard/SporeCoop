---
id: BUG-1279
title: Space: a guest in the host's world has no property list of the host's captain (its item list), the captain screen shows no item limits
status: open
kind: bug
stage: Space
reported: 2026-11-18
source: static RE for GitHub issue #203
role: guest
versions: 0.7.15.1
fixed_in:
branch: feat/w74-captain
commits: 596a872b, 6ac83cfb, 0bd01c0d
tests: space_captain_items, space_captain_items_off, space_captain
signature: 
docs: docs/re/space.md#space-captain-items-null, docs/re/creations.md#creations-captain-0717, docs/04-netcode.md#netcode-creation-captains
related: BUG-0304, BUG-0200, BUG-0017, BUG-1475
switch: creations.captain_props
---

## Report
Found while fixing BUG-0304 (GitHub #203), static. A guest in the host's Space world plays the host's empire, whose
captain (cEmpire +0xC4) is usually one of the host's own creations. The guest's game has that creature (the mod sends
it, BUG-0017 / BUG-0200) but not the captain's property list {captain, 0x00B1B104, group | 0xE500} (the captain's item
config 0xC339D5C7 and unlocked item keys 0xBEAC70B6 / 0xC285E3DB, the newer-version link 0x0D8A4BC9). Since 0.7.15.3
the guest's captain screen no longer crashes on it (BUG-0304) but shows no item limits (the game's "no items").

## Root cause
creations.cpp `records_from_game` (host_species_out) keeps only records of the creation's own group and type (plus its
pictures and creaturedata); the property list lives in group | 0xE500 with type 0x00B1B104 (written by 0x5BF220 next to
the creation). Observed in the reporter's guest log and the crash stack (292845D0 / 00B1B104 / 4062E500 looked up).

## Fix
creations_captain.cpp (`coop.fix creations.captain_props`, off = 0.7.16.1): in Space the host reads its empire captain's
property list from the game's own package file (only a captain's: it has the item config 0xC339D5C7; without the
newer-version link 0x0D8A4BC9) and sends it as MSG_CREATION kind 10 to each guest of creations 1.2 (an older guest gets
nothing); again on a captain change, on the way back into Space and every 30 s. The guest registers it under the host's
key in its own package at priority 500 (below EditorSaves), not when it has its own list of that key or its own
EditorSaves creation of the captain's id (BUG-0278's class), and drops cPropManager's cached list of the key
(RemovePropertyLists vt 0x3C). The hook on SporeCoop::CaptainProps_Save (now verified: arg 3 = async; every caller but
CaptainProps_Copy drops the result) refuses to save a list of ours (that host list on a connected guest, a 0x7A captain
copy, a creation of a SporeCoop package) so none reaches Pollination.package; the guest's own creation under that id is
saved as the game does. Port of the idea of 4e7490e6 (fix/agy-open-bugs) with the review's findings fixed (#8 signature
and callers, collision case, #11/#12 only the Space captain's list is sent). Rules: captain_logic.h (host_prop,
refuse_props_save, strip_prop).

## Verification
open, waiting for the lab run (no game run on this branch by day). Built, `make test` passes (tests/core_tests.cpp captain_rules). pending night run: space_captain_items (PASS: the guest's
CaptainProps_Get finds the host captain's list, props=1, and its captain panel by Accept and the rank badge gets an item
list: BUG-0304's "items: none" unchanged) and its twin space_captain_items_off (SPORE_FIX_OFF=creations.captain_props,
must FAIL); regressions near it: space_captain, space_captain_none, space_coop. Old-peer run against 0.7.16.1 in the
final check (creations 1.1: no kind 10 sent). ~ Not known yet: whether the galaxy menu's template 0x15A44C0 that `coop.captain
plant` copies holds the item config (the test says so on the host's line).

## History
- 2026-11-18: opened while fixing BUG-0304 (GitHub #203), for 0.7.16 or later.
- 2026-11-23: feat/w74-captain (WP-K1): fix, unit tests, lab test and twin written; waits for the night run.
