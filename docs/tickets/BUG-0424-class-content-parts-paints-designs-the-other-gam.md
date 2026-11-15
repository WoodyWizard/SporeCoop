---
id: BUG-0424
title: class: content (parts, paints, designs) the other game lacks (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content
commits: 9e2dadf1 8c11d8d4 a10f59bd dffd1a16
tests: space_zero_key, space_host_ship_design, civ_design_content, content_own_cell, content_own_ufo, content_audit_tribe, content_audit_civ, content_audit_space, space_mirror_model
signature:
docs: docs/re/creations.md
related: BUG-0723, BUG-0724, BUG-0725, BUG-0726, BUG-0727, BUG-0728, BUG-0729, BUG-0730, BUG-0731, BUG-0452, BUG-0469, BUG-0559, BUG-0643, BUG-0647, BUG-0670
---

## Report
Fixed per case: BUG-0341, 0354, 0369 (hard-coded Creepy & Cute tables, creature models only, creations.cpp:280-336), BUG-0323 (Space NPC ships).
Siblings not covered: parts/effects from ModAPI or other part mods, edited packages, partial installs (0x441F3B fault class) (inf); cell, vehicle, building, UFO, city models never checked; Civ vehicle and building mirrors spawned from design keys with no key check (civ.cpp ~1909-1915, MSG_CIV_BUILD) (inf crash risk); zero key still goes to SpawnUFO (squadron.cpp:447) (obs); unknown ship model -> invisible host ship that shoots (obs); host's own vehicle, building and Space designs never reach guests (creations.h:22 creature/cell/ufo only) (obs); creations.cpp:986 comment wrong.
General fix: ensure_content(key) with key_exists for every referenced id of every model kind, substitute or refuse; transfer bld/vcl designs with id remapping. Outcome check: host creation with unknown part ids in Creature / Civ / Space: guest shows a model, no fault.
Source: docs/generalisation-plan.md G7.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11) of the sibling list and the History's open items against rel/0.7.11: the zero ship key, the host's own ship designs, part descriptions of vehicles and buildings, buildings left out, the untested cell / own UFO lookalike were open; unreadable records, a pack-only animation, tribe tools / huts and city decorations were checked.

## Fix
Shared method: creations::ensure_model / host_design for every model kind with the sender's part descriptions kept for the session. Children: BUG-0723 fixed (fleet.zero_key), BUG-0724 fixed (fleet.host_designs, the wrong creations.cpp comment corrected), BUG-0725 fixed (content.design_lookalike), BUG-0726 fixed (content.building_copy), BUG-0728 fixed (tests), BUG-0727, 0729, 0730, 0731 not-reproduced with their evidence.

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-02 (unreleased, BUG-0469, fix/w39-content-all): `creations::ensure_model` checks and fixes any model the mod makes a game build, behind `content.all_kinds`: Civ mirror vehicles (a substitute copy, or the game's own design of that purpose and locomotion for a design that cannot be built), Civ buildings (the design cannot be replaced, so a design this game lacks is left out), the parts and paints inside a host NPC ship's model in Space (not only its key), and the paints of vehicle, UFO and building models. Cell paints stay unchecked (a cell model's header is not the creature one: it names ids no package holds as an effect record). Tests content_civ / content_space (FAIL and PASS runs in the ticket).
- 2026-11-02 (0.7.3.5, BUG-0452): generic check (key_exists on every part id, base game substitutes, refuse otherwise) now covers creature, cell and UFO models on every path that registers received creation records (a player's creation, NPC species served by the host, the host's own creations of its world, the world load). Still open: Civ vehicles and buildings, city designs, the host's NPC ship models in Space (squadron SpawnUFO), paints of cell and UFO models, tribe tools / huts if they reference pack parts. Target 0.7.4 (one ensure_content for every kind; stage-independent).
- 2026-11-04 (unreleased, BUG-0559, fix/w43-content): one mechanism for any part a receiving game lacks: the
  sender's description of the part, the most similar part of the receiver's own installed packages (role,
  abilities, size); paints by the game's effect directories; the species data (creaturedata) of a bundle follows
  its model (`content.lookalike`, `content.species_data`; test content_lookalike). Still open: vehicles and buildings
  (`ensure_model` gets no description: the tables), cell and UFO parts not tested, an animation only the pack part has.
- 2026-11-06: Discord report 2026-11-06 (0.7.8): children BUG-0647 (the host's vehicle designs shown as monsters on the guest), BUG-0643 (white roster portraits, or 0431).
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
- 2026-11-08: released in 0.7.11.
