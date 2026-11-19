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
branch:
commits:
tests: 
signature: 
docs: docs/re/space.md#space-captain-items-null
related: BUG-0304, BUG-0200, BUG-0017
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
Not done (not a hotfix): send that resource with the host's own creation and register it on the guest under the
HOST's key (the save names the captain by it), in its own package, not aliased; mind cEmpire 0xC32AE0 following
0x0D8A4BC9 on the guest, the creations bundle format of older guests (msg_manifest minor bump) and that the guest's
game can write the list back (0x5BF220 at an adventure's end): it must not reach the host's save.

## Verification
no-fail-run: open, not fixed.

## History
- 2026-11-18: opened while fixing BUG-0304 (GitHub #203), for 0.7.16 or later.
