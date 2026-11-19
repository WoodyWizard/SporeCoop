---
id: BUG-1320
title: Tribe: SetSpecies on a young member reads past the citizen object (cCreatureAnimal cache at +0x1628, fault at 0xFFFFFFFF)
status: duplicate
kind: crash
stage: Tribe
reported: 2026-11-19
source: lab
role: 
versions: 
fixed_in:
branch: fix/w57-savestrip
commits: 9a1a1d00
tests: tribe_own_species
signature: exception 0xC0000005 at 0xFFFFFFFF, calls 00BC326E 00C03A94 (cCreatureAnimal::SetSpecies+0x174)
docs: docs/re/tribe.md#tribe-own-species
related: BUG-1313, BUG-1271, BUG-1299
---

## Report
tribe_own_species, lab 20261119-113954 (lane 4): every check passed, the test failed on a caught fault on the host 0.5 s
after the guest's line came back in the new evening: `exception 0xC0000005 at 0xFFFFFFFF`, calls 00BC326E 00C03A94
(cCreatureAnimal::SetSpecies+0x174), right after "member #4 (owner 1) is now of species 7B635F66"; the next member of
the pass was #6, the baby the guest had bought before the save (young after the load).

## Root cause
Observed (Ghidra 2026-11-19): cCreatureAnimal::SetSpecies 0xC03920 ends, for a creature that is not adult (+0xB34 != 1),
not the avatar (+0xB58 bit 0x200) in the Creature or Tribe mode, with LazyU64_Get 0xBC3260 on creature +0x1628 (LEA
ECX,[ESI+0x1628] at 0xC03A89): when the byte +0x18 there is 0 it calls the pointer at +0x1C and writes +0x10..+0x18.
That is a cCreatureAnimal field (0x16A0 bytes, alloc 0xB1ED2D); a tribe member is a cCreatureCitizen of 0x1038 bytes
(alloc 0xB1ED5D): +0x1628..+0x1644 are another heap object's bytes. The fault's ECX 10A9FA00 = ESI 10A9E3D8 + 0x1628.
WP-T1 (BUG-1271) gives members their owner's species with this SetSpecies; adults skip the branch, a young member
after a load (or any other young member the pass changes) reads, calls or writes past the citizen. Births of BUG-1299
take the species at the spawn and never run it. Inferred: earlier runs passed because the bytes there happened to
have the valid byte set (no call, no write).

## Fix
Duplicate of BUG-1313 (fix/w57-tribe-keys, merged into rel/0.7.16 while this branch worked: `tribe.kid_species`, a
kid waits until it is grown up, no SetSpecies on it). The hook described below (LazyU64_Get skipped for our own
SetSpecies, `tribe.species_young`, commit 9a1a1d00) was taken out again in the merge of rel/0.7.16 (one fix for one
bug); its Ghidra notes stay here and in docs/re/tribe.md#tribe-own-species.
What this branch had: tribe_species.cpp: every SetSpecies of ours on a tribe member goes through `set_species_member` (also `coop.tribe
setspecies`), which marks the member; a hook on LazyU64_Get 0xBC3260 skips the call whose cache is that member's
+0x1628 (nothing read, called or written past the citizen). Counter `young_skipped=` in `coop.tribe species` /
`coop.status`. Switch `coop.fix tribe.species_young` (off: the game's call).

## Verification
* no-fail-run: the fault depends on the heap bytes after a 0x1038-byte citizen (a call only when the byte at +0x1640
  is 0); seen once in lab 20261119-113954 (tribe_own_species, host fault at 0xFFFFFFFF, calls 00BC326E 00C03A94).
  With the fix: lab 20261119-114816 and 20261119-120449 tribe_own_species, `young_skipped=2` (the bought baby given its
  species twice), 0 faults on both games.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-savestrip.
- 2026-11-19: duplicate of BUG-1313 (merged first); this branch's hook removed in the merge of rel/0.7.16.
