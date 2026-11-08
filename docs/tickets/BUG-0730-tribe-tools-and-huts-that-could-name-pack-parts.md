---
id: BUG-0730
title: Tribe: tools and huts that could name pack parts (verify)
status: not-reproduced
kind: bug
stage: Tribe
reported: 2026-11-08
source: BUG-0424 sibling list, K2b audit 2026-11-08
role: 
versions: 
fixed_in:
branch: fix/w46-k2b-content-b
commits: e896b4a8
tests: content_audit_tribe (coop_content_audit.py tribe)
signature:
docs: docs/re/creations.md#creations-k2b-design-content
related: BUG-0424
---

## Report
K2b audit of the class BUG-0424 (BUG-0452's open point): Tribe tools and huts might reference parts of a pack the other game lacks (to verify and close with evidence).

## Root cause
Observed (lab 20261108-033541 and 20261108-034334, content_audit_tribe, both games, `coop.creations objmodels scan`): the Tribe world's tools (noun 0x18C8F0C) are drawn from property files of the game's PatchData.package (49CC4358 / CE1C5CE3, group F37C24E6, at object +0xC4), the huts (noun 0x1E4DAAE) hold three property keys of PatchData.package (group 2BE764E2, at +0x1E8 .. +0x200: the hut's levels), the fruit, rocks and decorations PatchData / Spore_Graphics property files: no Tribe object names a player's creation (EditorSaves / Pollination) or a pack model, on the host and on the guest; the only creations in a Tribe world are its creatures (the species, covered by BUG-0452 / BUG-0559).

## Fix
None needed. The audit stays as a test (content_audit_tribe) and command (`coop.creations objmodels [scan]`).

## Verification
no-fail-run: nothing to fix; evidence out/lab/20261108-033541, out/lab/20261108-034334 (content_audit_tribe OK, the rows in the logs).

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): not reproduced: the tools and huts are the game's own models (audit runs above).
