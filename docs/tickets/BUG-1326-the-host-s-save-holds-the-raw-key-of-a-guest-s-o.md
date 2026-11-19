---
id: BUG-1326
title: The host's save holds the raw key of a guest's own Tribal Outfitter creation in lastSave.pld and PlanetScripts.pld (and the outfit's line key in the .spo)
status: open
kind: bug
stage: Tribe
reported: 2026-11-19
source: lab
role: host
versions: rel/0.7.16
fixed_in:
branch:
commits:
tests: tribe_own_outfitter, own_editor_drop
signature:
docs: docs/known-issues.md
related: BUG-1272, BUG-1315, BUG-1298, BUG-1316
---

## Report
Found by fix/w57-own-editors (not its area: the save strip, BUG-1315). Once the guest's own Tribal Outfitter works
(BUG-1308, BUG-1316), tribe_own_outfitter and own_editor_drop reach their save check and fail it: lab
20261119-122505 tribe_own_outfitter "the host's save names no key of the guest's (outfit 7B562193, creation 2929094E
...: ['Zorispe.spo:7B562193', 'lastSave.pld:2929094E' x2, 'PlanetScripts.pld:2929094E' x2])", own_editor_drop the same
with 7B4A7F48 / 292908E2. 2929094E is the guest's own outfit creation (its My Creations key), 7B... its line key.

## Root cause
Not analysed. The .spo hit is BUG-1315's class (line keys written by the members). The raw creation key in
lastSave.pld / PlanetScripts.pld is new: those streams are not the .spo; something of the host's game (planet scripts:
perhaps the tribe outfit history or the Sporepedia's seen list) records the guest's raw creation key that the line's
bundle registered on the host. To reproduce: `lab.py run tribe_own_outfitter`.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
- 2026-11-19: after BUG-1315 (savestrip) was merged the .spo hit is gone (fix/w57-own-editors 20261119-142554,
  -153243); the raw creation key stays in lastSave.pld and PlanetScripts.pld, in some runs also in planetRecords.pkp /
  .pkt (20261119-130049, -153243 own_editor_drop). Not analysed further; a guess to check first: the line's records are
  registered on the host under the 0x7B key, but the creation's own metadata still names its raw key (cAssetMetadata
  +0x20, docs/re/creations.md, fork note), and the game's planet records / scripts store that asset key.
