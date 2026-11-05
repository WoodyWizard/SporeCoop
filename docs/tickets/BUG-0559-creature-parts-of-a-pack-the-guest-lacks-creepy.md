---
id: BUG-0559
title: Creature: parts of a pack the guest lacks (Creepy & Cute) are drawn as big blocks, and the host's colours are another shade on the guest
status: fixed
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in: 0.7.7
branch: fix/w43-content
commits: acf49d4e f29a52d3
tests: content_lookalike (tools/tests/coop_content_lookalike.py)
signature:
docs: docs/re/creations.md#creations-lookalike
related: BUG-0424, BUG-0452, BUG-0369, BUG-0354
switch: content.species_data, content.lookalike
---

## Report
The host: the guest sees some parts of the host's creature as big blocks (different from part to part); the guest does not have the Creepy & Cute pack. On Discord: the host's colour is a completely different shade on the guest's screen. Before 0.7.4 this crashed; now a fallback part is drawn, it should be a similar part.
Reports: SporeCoop-report-2026-11-04 184642 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s4687: only the host's screen (its creature looks right there); no guest screenshot of the blocks.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (guest log 17:28:33): "Creepy & Cute parts pack NOT present", then "the host's own creation
  29116124: part A115CFC8 unknown here" (and 82A9416E): the guest draws the fallback part of BUG-0452
  (creations.unknown_parts) instead of a similar part. Colours: not in the logs.
* CORRECTION (2026-11-04, fix/w43-content): A115CFC8 and 82A9416E ARE Creepy & Cute parts with table substitutes;
  the guest log shows them substituted ("7 part(s) ... shown as base game parts (similar base game parts)"). The
  "unknown here" lines are logged before the substitution. The blocks have another cause:
* Observed (lab, lanes 9 / 10, `coop_content_lookalike.py`, run `out/lab/20261104-225313` with manual screenshots):
  **blocks**: the host's species data (creaturedata 0x0F43029A, sent with the host's own species of its world) names
  every part a second time (`00 60 62 40` + id, not byte-run packed, with the part's box) and was not touched by the
  substitution. Carol's game (no Creepy & Cute) drew the host's family and pack members with orange boxes in place
  of the pack's parts, while the model it reads had only substitutes (`coop.creations partlist`). The same
  substitutes put into the host's model directly (no pack part) are drawn normally (run with
  `CONTENT_SWAPS=08504962=CE3E4186,...`, `out/lab/20261104-231026`). The player's guest log has the same path
  (`the host's own creation ... of its world is 7D......`, with creaturedata).
* Observed: **colours**: the host's creature kept the base game's Cell paint `skinpaint_cell_stripes` (0xE4C02CCE);
  it has no effect record of its own (it is compiled into the base game's effect directory) and was not on the
  mod's list of base paints, so every other game, with or without the pack, painted that layer with the fallback
  `skinpaint_base_blankShiny` (lab: `paints 290229B9 0156AC06 ...` on Carol; the player's guest log: "paint E4C02CCE
  unknown here ... a base game paint"). The species data also named the pack's paint (0xC78AAED4).
* Observed: a part of a pack that is not Creepy & Cute (Spore_Pack_03's foot 0xF54F0B90) became the fixed detail
  0x0330D834: the creature lost its feet on that game (the 0.7.6 fallback has no role).

## Fix
* `content.species_data` (`creations.cpp` `align_species_data`): the part and paint ids substituted in the bem are
  substituted in the bundle's creaturedata too; one that still names a part this game lacks is left out (the game
  computes its own). Off: 0.7.6.
* `content.lookalike`: (1) `paint_known` asks the game's effect directories
  (`Swarm::cEffectsManager::GetDirectoryAndEffectIndex(id, 0)`) first, so a paint this game has is never replaced;
  (2) the sending game adds each part's own property list to the bundle (group 0x7C0DE5C1, never registered), the
  receiving game picks among the part props of ITS packages (the resource manager's database list) the part of
  the same role, the nearest abilities, ranks, cost and size (`mods/coop/net/partprops.*`, unit test and fuzzer);
  no part of that role: the 0.7.6 chain. Off: 0.7.6 (the tables and the fixed detail, the paint lists).
* `coop.creations partlist <key>` (tests): each part of the model the game reads with its role, and the parts of
  its species data this game lacks.

## Verification
* PASS (both switches on): `out/lab/20261104-231620` (lane 9 + 10): the host's species data on Carol names 33
  parts, none she lacks; the model has an eye, a mouth and a foot where the host's has them (the Spore_Pack_03
  foot is the base foot 0x7F815C91), the base paint is the host's 0xE4C02CCE, the pack's detail paint replaced,
  the species built, 0 faults; screenshot `carol_sees_host.png` in the run folder (no blocks).
* FAIL `SPORE_FIX_OFF=content.species_data`: `out/lab/20261104-231837` (species_data_unknown=13).
* FAIL `SPORE_FIX_OFF=content.lookalike`: `out/lab/20261104-232034` (the foot is a detail on Carol, the base
  paint 0156AC06 in place of 0xE4C02CCE).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (C1), observed in the guest log.
- 2026-11-04 (fix/w43-content, C1 of 0.7.7): root causes found in the lab (species data, the cell paint, the
  fallback without a role); fixed behind `content.species_data` and `content.lookalike`; test content_lookalike.
- 2026-11-05: released in 0.7.7.
