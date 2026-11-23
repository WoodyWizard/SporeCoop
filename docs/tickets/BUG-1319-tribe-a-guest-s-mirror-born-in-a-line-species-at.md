---
id: BUG-1319
title: tribe: a guest's mirror born in a line species at a world's opening is forgotten by the world reset and keeps that species when its member is another player's
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-19
source: lab
role: guest
versions: 0.7.16 (unreleased)
fixed_in: 0.7.16
branch: fix/w57-tribe-keys
commits: 2da18ac6
tests: tribe_stray_line_key, tribe_stray_line_key_off (coop_own_editors_robust.py stray)
signature: late_species_rowclear_off: guest: #1:0:<guest's 0x7C key> (a host member in the guest's creature)
docs: docs/re/tribe.md#tribe-kid-setspecies
related: BUG-1271, BUG-1299
---

## Report
Lab 20261119-112300, late_species_rowclear_off step 4: after the guest's game was started again and joined, the
host's chief #1 on the guest's game wore the guest's creature 7C382858 for 40 s (the host's game: the tribe's species).

## Root cause
✔ (guest log) At the world's opening a mirror was spawned for a guest-owned member with tribe.birth_species
("member #0 (owner 255) is born of species 7C382858"), the world's open then ran the species pass's WORLD_STATE reset
(its table of members the mod changed is cleared), and the mirror ended bound to the host's member #1. A member the
mod never touched keeps its species (species_act: Keep without a record), so nothing put it back. ~ Why the mirror
went to #1 (the bind by nearest local member at a world's opening) is not checked.

## Fix
`tribe.stray_line_key`: a member that shows a line's key (a shared 0x7B, creations' 0x7C copy, or a key of a current
line) and has no record in the pass's table is taken as the mod's, so the pass gives it its owner's line or the slot's
(the game never gives a member such a key). Off: kept. Test: on the guest's game a grown-up host member gets the
guest's line key by the game's SetSpecies (`coop.tribe setspecies <key> <id>`, new member argument).

## Verification
FAIL (twin, SPORE_FIX_OFF=tribe.stray_line_key): tribe_stray_line_key_off 20261119-113824 (the host's member keeps the
guest's 7CBC3253 on the guest's game). PASS: tribe_stray_line_key 20261119-113824, merged tree 20261119-115724.
Seen in the wild: late_species_rowclear_off 20261119-112300 step 4 (before the fix); the same twin in
20261119-113824 (with the fix) failed at step 3 only.

## History
- 2026-11-19: opened (fix/w57-tribe-keys).
- 2026-11-19: fixed on fix/w57-tribe-keys.
- 2026-11-23: released in 0.7.16.
