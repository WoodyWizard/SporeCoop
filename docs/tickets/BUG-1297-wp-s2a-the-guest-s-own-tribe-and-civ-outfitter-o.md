---
id: BUG-1297
title: WP-S2a: the guest's own Tribe and Civ outfitter open on the host's body when its creature's key is a key of the host's world
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-19
source: plan
role: guest
versions: 
fixed_in: 0.7.16
branch: feat/w56-editors
commits: 8709f9fe 800a17e6 2cb5861c
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
Night run 20261119-104136: own_editor_key FAILED in its set-up: the tribe's species 067B3E4A of the fixture is not in
the host's My Creations, so the host's bundle had no records and registered nothing on the guest (no shadow, the guest
answered its own body). The test now collides with a creature of the host's own My Creations (TigerHorse 290229B9),
whose bundle registers the host's body on the guest (checked). Its twin had also run with the switch on (BUG-1314).
The BUG-1297 check: PASS 20261119-112300 (the outfitter asked for 7CF30A6D: the guest's own body 9B6134E2, not the
host's 6E1A058F); twin own_editor_key_off FAIL 20261119-112300 for the right reason (asked for the raw 290229B9: the
host's body 6E1A058F). The test as a whole still FAILS after that check: the guest's own outfitter does not come back
to Tribe after Done (`the guest is back in Tribe (0x00DBDBA1)`), as in tribe_own_outfitter and own_editor_drop of the
night run (the own outfitter's return, fix/w57-own-editors' area); on the merged tree (20261119-115724,
20261119-121054) its hut planner did not open either (coop_tribe_outfit.open_outfitter, 3 clicks; the guest had just
joined from its galaxy menu: "the galaxy menu's splash window ... was up in the stage: hidden (BUG-0938)" in both).
Open until the outfitter's return is fixed and the test passes whole.
Fixed (fix/w57-own-editors, merged tree with fix/w57-tribe-keys): the return was BUG-1308 (Done blocked by the part-list
error of the guest's own creation) and BUG-1316 (its result never taken); the planner after a join from the galaxy menu
now waits for the game's time and clicks the hut once more after 5 s when no planner came (coop_own_editors_robust.py
open_own_outfitter; in the run it opened at once: "game time runs after 0.03 s; hut on screen (699, 433)"). PASS whole:
fix/w57-own-editors worktree out/lab/20261119-130049 own_editor_key (the outfitter asked for 7CF30A6D: the guest's own body 9B6134E2; back in Tribe; the
slot the host's; both games dress the guest's members in its new outfit 7BF92563 / 954ADE35). Twin own_editor_key_off
FAIL in the same run for the right reason (the outfitter opens on 290229B9 -> 6E1A058F, the host's body).

## History
- 2026-11-19: opened.
- 2026-11-19: feat/w56-editors: fix and its test written (switch, FAIL twin entry); the lab run waits for the orchestrator.
- 2026-11-19: fix/w57-tribe-keys: set-up fixed (host My Creations key), BUG-1297 check PASS / twin FAIL; the test waits for the own outfitter's return (Done).
- 2026-11-19: fix/w57-own-editors: own_editor_key PASS whole / twin FAIL (20261119-130049): fixed.
- 2026-11-23: released in 0.7.16.
