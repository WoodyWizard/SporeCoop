---
id: BUG-0452
title: creature: a Steam guest crashes at 0x41F3B when it builds a host's NPC species with a part its game lacks that is not on the Creepy & Cute list
status: fixed
kind: crash
stage: Creature
reported: 2026-11-02
source: player report SporeCoop-report-20261102-223006.zip (0.7.3.4, both Steam, guest without Creepy & Cute; 3 crashes)
role: guest
versions: 0.7.3.4
fixed_in: 0.7.3.5
branch: fix/w38-content
commits: 2089bed2, ddcc43d9, 96558106
tests: unknown_parts (tools/tests/coop_unknown_parts.py)
signature: 3010a413:SporeApp.exe+0x41F3B read 0x00000000 (dc04aee5 0x441F3B, FUN_00441910+0x62B, model bake: this+0xC property list null)
docs: docs/re/creations.md#creations-unknown-parts
related: BUG-0341, BUG-0354, BUG-0369, BUG-0424
switch: creations.unknown_parts
---

## Report
Both players on Steam (3010a413, the GOG 2024 code). The guest's game has no Creepy & Cute pack. The
guest loads the host's Creature world, asks the host for an unknown NPC species (`creations: species
291313A6 of the host's world is unknown here: asked the host`), registers it (`... registered (2
records)`) and crashes at once at `SporeApp.exe+0x41F3B` (read of 0) while the game bakes the model
(EditorModel load -> cEditor::LoadCreatureData -> cSpeciesManager::GetSpeciesProfile). Three crashes in
a row. The species has no Creepy & Cute part (no "substituted" line), so it uses a part (or paint) of
another source the guest lacks.

## Root cause
Observed (lab, lanes 4/5, `coop_unknown_parts.py`): a guest whose game lacks `Data/Spore_Pack_03.package`
and a host creation (Tiger Horse, 290229B9) with one part from that pack (`0x75A5D2D4`) faults at exactly
`0x00441F3B` (data 0) while the guest's game bakes the creation (crash reports
`out/lab/20261102-122525/unknown_parts#*.report-*.txt`, Carol in LoadGame). The host (with the pack)
builds the same creation without a fault. The same fault as BUG-0341: the bake reads a null property list
for a part id this game does not have.

The BUG-0341/0354/0369 checks only know the hard-coded Creepy & Cute part and paint lists, so a part of
another pack passed them.

A second cause was in the part key parser: it looked for the literal four group bytes (`00 60 62 40`),
but the group's leading `0x00` is usually encoded as a byte run (the byte before, repeated), so a 33-part
model yielded one reference and the unknown part was never found (observed: the patched Tiger Horse model
has 33 `60 62 40` keys but the new parser found 1).

Inferred (not needed for the fix): the reported species (291313A6) uses a part of a pack the guest lacks,
which is not on the Creepy & Cute list (another parts pack such as the Bot Parts pack, a patch, or a
modded part).

## Fix
`creations.unknown_parts` (on by default; `coop.fix creations.unknown_parts off` = 0.7.3.4, only the
Creepy & Cute list):
* `creations.cpp`: every part id of every received creature, cell and UFO model is read with
  `model_part_refs` (the group's last three bytes, then the instance; slot and parent must be plausible
  so that the group bytes in other data are not taken for parts) and checked with `key_exists` (cached per
  session: `part_known`). Every paint effect id of a creature model is checked the same way (`paint_known`,
  plus `kBasePaintEffects` for the 14 base game paints that have no effect record).
* Unknown part: the Creepy & Cute table first (creature parts), then a base game detail of the same group
  and the same number of `0xFE` bytes (`kBasePartFallbacks`); the id is rewritten in this game's copy of
  the model. Unknown paint: the Creepy & Cute table first, then a base game paint of the same layer and
  length (`kBasePaintFallbacks`).
* An id that cannot be replaced refuses the creation: a player's puppet falls back as for an unknown
  creation, an NPC species of the host's world is not registered, and the host's own creation of its world
  is not registered so the world is not loaded (the message in `world.cpp` now names the generic case).
* Each unknown id is logged (`creations: <species/creation>: part <id> unknown here: this game does not
  have it (...)`; paints likewise).
* Paths covered: `apply` (a player's creature, cell and UFO; the `Kind` enum has only these three, there
  is no vehicle or building creation path), `apply_species` (an NPC species of the host's world served on
  request) and `apply_own_species` (the host's own creations of its world). Paints are checked for
  creature models only (the cell/UFO model header is not verified for the three effect dwords).
* `content_packs.h`: `kBasePaintEffects` (111 base game skin paint effects from
  `Data/Spore_Game.package` and `Data/PatchData.package`), `kBasePaintFallbacks` and `kBasePartFallbacks`.

## Verification
* FAIL (fix off, `SPORE_FIX_OFF=creations.unknown_parts`): `out/lab/20261102-122525` (lanes 4/5, 2 repeats).
  2/2 tests FAIL; each repeat wrote a crash report with `0xC0000005 x2 at 0x00441F3B
  SporeApp.exe+0x41F3B data=0x00000000` (Carol in LoadGame, no unknown-id log).
* PASS (fix on): `out/lab/20261102-122224` (lanes 4/5, 2 repeats). 2/2 OK: Carol does not crash, plays
  the host's Creature world, logs the unknown part id, and the model her game reads has 33 parts, none
  unknown, the species built (`parts 290229B9 n=33 cc=0 unknown=0 species=1 have_pack=1 substituted=2
  refused=0`).
* Regression: `out/lab/20261102-124102`
  (`smoke,creations,guest_pack_paints,guest_missing_pack,legless_guest,old_species,nest_species,creature`
  on lane 4, free lane 5): 20/20 PASSED. `make -j8` and `make test` pass.

## History
- 2026-11-02: opened from the guest's report zip (0.7.3.4, three crashes with minidumps and logs).
- 2026-11-02 (fix/w38-content): generic part and paint check behind `creations.unknown_parts`; the part
  parser now finds every key (the group's `0x00` is a byte run); test `coop_unknown_parts.py` (FAIL and
  PASS lab runs above).
- 2026-11-02: released in 0.7.3.5.
