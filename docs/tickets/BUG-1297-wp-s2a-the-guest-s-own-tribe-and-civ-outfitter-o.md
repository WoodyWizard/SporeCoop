---
id: BUG-1297
title: WP-S2a: the guest's own Tribe and Civ outfitter open on the host's body when its creature's key is a key of the host's world
status: open
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in:
branch: feat/w56-editors
commits: 8709f9fe
tests: own_editor_key (coop_own_editors_robust.py key), own_editor_key_off
signature: species::own_key
docs: docs/re/creations.md#creations-own-editors-robust, docs/06-testing.md#testing-own-editors-robust
related: BUG-0278, BUG-1255, BUG-1272, BUG-1275
---

## Report
Lineages comparison (out/research/lineages-species.md, item 2): `species::own_key(Stage::Creature)` returned the raw My
Creations key, while creations remaps an own key that is also a key of the host's world (BUG-0278). With such a
collision (common after BUG-1255: the guest's first creature made from the host's in the landfall editor, "Save New"
keeps the host's key) the guest's own Tribal Outfitter (BUG-1272) and Civ outfitter (BUG-1275) would open on the
host's body. The fork met the same class for ships (its BUG-9071).

## Root cause
✔ (code) mods/coop/species_line.cpp own_key used `creations::mine(kCreature).key`; designs.cpp own_outfitter and
civ_editors.cpp h_Submit put it into the editor request. On the guest the host's world package (apply(), priority
2000 above My Creations) answers that key with the host's records whenever the host's creature bundle of that key was
applied in the session (creations.cpp own_key_here's own comment). Not run.

## Fix
`creations::own_here(kind)` (own_key_here while connected: the shared remapped 0x7C...... copy of our own records) for
the creature; for an outfit whose own key `creations::host_shadows`, the line's registered copy (0x7B......). Switch
`species.own_key_here` (off: the raw key, 0.7.16 before WP-S2).

## Verification
Not run yet: the lab waits for the orchestrator's word (2026-11-19, the CPU was busy with a foreign task). Ready:
`SPORE_DETECTOR=20 python3 tools/lab.py run own_editor_key own_editor_key_off --lane 6 --lane 7 --retry 0 --no-build`; the twin must
FAIL, the test PASS (run ids go here).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
