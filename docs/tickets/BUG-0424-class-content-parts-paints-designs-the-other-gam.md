---
id: BUG-0424
title: class: content (parts, paints, designs) the other game lacks (umbrella)
status: open
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Fixed per case: BUG-0341, 0354, 0369 (hard-coded Creepy & Cute tables, creature models only, creations.cpp:280-336), BUG-0323 (Space NPC ships).
Siblings not covered: parts/effects from ModAPI or other part mods, edited packages, partial installs (0x441F3B fault class) (inf); cell, vehicle, building, UFO, city models never checked; Civ vehicle and building mirrors spawned from design keys with no key check (civ.cpp ~1909-1915, MSG_CIV_BUILD) (inf crash risk); zero key still goes to SpawnUFO (squadron.cpp:447) (obs); unknown ship model -> invisible host ship that shoots (obs); host's own vehicle, building and Space designs never reach guests (creations.h:22 creature/cell/ufo only) (obs); creations.cpp:986 comment wrong.
General fix: ensure_content(key) with key_exists for every referenced id of every model kind, substitute or refuse; transfer bld/vcl designs with id remapping. Outcome check: host creation with unknown part ids in Creature / Civ / Space: guest shows a model, no fault.
Source: docs/generalisation-plan.md G7.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-02 (unreleased, BUG-0469, fix/w39-content-all): `creations::ensure_model` checks and fixes any model the mod makes a game build, behind `content.all_kinds`: Civ mirror vehicles (a substitute copy, or the game's own design of that purpose and locomotion for a design that cannot be built), Civ buildings (the design cannot be replaced, so a design this game lacks is left out), the parts and paints inside a host NPC ship's model in Space (not only its key), and the paints of vehicle, UFO and building models. Cell paints stay unchecked (a cell model's header is not the creature one: it names ids no package holds as an effect record). Tests content_civ / content_space (FAIL and PASS runs in the ticket).
- 2026-11-02 (0.7.3.5, BUG-0452): generic check (key_exists on every part id, base game substitutes, refuse otherwise) now covers creature, cell and UFO models on every path that registers received creation records (a player's creation, NPC species served by the host, the host's own creations of its world, the world load). Still open: Civ vehicles and buildings, city designs, the host's NPC ship models in Space (squadron SpawnUFO), paints of cell and UFO models, tribe tools / huts if they reference pack parts. Target 0.7.4 (one ensure_content for every kind; stage-independent).
