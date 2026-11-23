---
id: BUG-1326
title: The host's save holds the raw key of a guest's own Tribal Outfitter creation in lastSave.pld and PlanetScripts.pld (and the outfit's line key in the .spo)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-19
source: lab
role: host
versions: rel/0.7.16
fixed_in: 0.7.16
branch: fix/w58-savestrip2
commits: a70bcafd
tests: tribe_own_outfitter, own_editor_drop
signature:
docs: docs/re/creations.md#creations-id-counter, docs/06-testing.md#testing-save-key-scan
related: BUG-1272, BUG-1315, BUG-1298, BUG-1316
---

## Report
Found by fix/w57-own-editors (not its area: the save strip, BUG-1315). Once the guest's own Tribal Outfitter works
(BUG-1308, BUG-1316), tribe_own_outfitter and own_editor_drop reach their save check and fail it: lab
20261119-122505 tribe_own_outfitter "the host's save names no key of the guest's (outfit 7B562193, creation 2929094E
...: ['Zorispe.spo:7B562193', 'lastSave.pld:2929094E' x2, 'PlanetScripts.pld:2929094E' x2])", own_editor_drop the same
with 7B4A7F48 / 292908E2. 2929094E is the guest's own outfit creation (its My Creations key), 7B... its line key.

## Root cause
docs/re/creations.md#creations-id-counter. The host's save held no key of the guest's: the test's scan was wrong.
* Observed (lab 20261119-164409, the host's slot copied and parsed record by record): the only 29293F61 (the guest's
  new outfit creation) in the slot is the index entry 00B1B104 / 4084AF00 / 29293F61 of lastSave.pld and
  PlanetScripts.pld (the same file): a planet script of the HOST's own, written at the load (16:44:25), forty seconds
  before the guest's outfitter saved 29293F61; with it 29293F5F (4084A100) and 29293F60 (4084B100). No unpacked record
  of any file of the slot names 29293F61, 7B519F39, 7CBC3253 or 290225CD. The host's line package of the outfit holds
  no 29293F61 either (the earlier guess, the creation's metadata naming its raw key, is wrong).
* Observed: new resource ids come from a counter per game process giving out consecutive ids; inferred from the
  values: it starts at the seconds since 2005-01-01 at the game's start (29293F5F = 16:44:15, the host booted at
  16:44:14). Two lane games start in the same second, so a guest's new creation gets the instance of one of the host's
  own new planet scripts or planet records (every hit of the earlier runs was 0x2929...., the session's seed).
* `keys_in_slot` (coop_tribe_own_species.py) searched each save file's RAW bytes for the instance (little or big
  endian): it matched the DBPF index of the host's own resources, and it could miss a key inside a compressed record.

## Fix
Test only (no mod change: the host's save is clean). `keys_in_slot` reads each DBPF file record by record (index and
unpacked data): a shared key (0x7B line, 0x7C copy) counts wherever it is; a raw instance counts in a record's data
and as the instance of a creation-type resource in an index, except where it is the id of one of the host's own
resources of this slot and no creation type stands within 8 bytes of it (every key layout has its type beside the
instance). The tests print those instances ("the host's own ids among them"). `savekeys_selftest.py` (make test,
unit-lab) checks the rule on synthetic slots. New twin `tribe_own_outfitter_keys_off` (SPORE_FIX_OFF=savestrip.keys):
the scan must still catch the members' line key in the .spo. No switch (a test change).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
* FAIL (the old scan): lab 20261119-164409 tribe_own_outfitter (lastSave.pld / PlanetScripts.pld: 29293F61, the host's
  own planet script's id); fix/w57-own-editors worktree 20261119-153243, -142554, -130049 (own_editor_drop, also
  planetRecords.pkp / .pkt).
* PASS (the new scan): lab 20261119-164857 tribe_own_outfitter (the host's own ids among the keys: 29294081, not
  counted) and own_editor_drop (292940E7).
* Twin FAIL for the right reason: lab 20261119-164857 tribe_own_outfitter_keys_off: "Zorispe.spo:7B7A1B64 (records
  018EB4B7)", the guest's members' line key (the scan still finds a real key, also unpacked).
* make test: savekeys_selftest.py (its first case FAILs with the raw-byte scan: the index id).

## History
- 2026-11-19: opened.
- 2026-11-19: after BUG-1315 (savestrip) was merged the .spo hit is gone (fix/w57-own-editors 20261119-142554,
  -153243); the raw creation key stays in lastSave.pld and PlanetScripts.pld, in some runs also in planetRecords.pkp /
  .pkt (20261119-130049, -153243 own_editor_drop). Not analysed further; a guess to check first: the line's records are
  registered on the host under the 0x7B key, but the creation's own metadata still names its raw key (cAssetMetadata
  +0x20, docs/re/creations.md, fork note), and the game's planet records / scripts store that asset key.
- 2026-11-19: fix/w58-savestrip2: no writer: the hit is the host's own planet script id from the game's clock-seeded
  id counter (equal to the guest's creation id when both games start in the same second); the save scan reads records
  now; tribe_own_outfitter and own_editor_drop PASS (20261119-164857), the savestrip twin FAILs on the .spo line key.
- 2026-11-23: released in 0.7.16.
