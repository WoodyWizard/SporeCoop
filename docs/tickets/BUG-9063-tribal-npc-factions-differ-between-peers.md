---
id: BUG-9063
title: Freshly mirrored NPC villages have different native faction IDs
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Fresh Vantiegan alliance qualification
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev (source build)
branch: duo-species
commits:
tests: tools/tests/duo_tribe_faction_mapping.py
signature: host NPC 067CD01D faction21CE becomes guest faction21D0
docs: docs/re/tribe.md
related: BUG-0381 BUG-9033 BUG-9034
---

The fresh campaign creates NPC villages independently through SpawnNpcTribe.
Native faction allocation differs: species067CD01D is21CE on the host and
21D0 on the guest;06805906 is21CF/21D1;06785BC2 is21D0/21D2;
06663915 is21D1/21D3. Both registered founder IDs agree. Evidence:
build/local/tribal-friendly-roster.log and tribal-friendly-failure-state.log.

The performance harness passed the host-local faction to a guest-local
debug command, which returns No such tribe. No command was sent or rejected
by the host. The harness must resolve the guest target using the mirrored
citizen identity, as the actual UI does.

Production relationship snapshots also use raw host faction IDs as native
guest IDs. Numeric collisions can apply records to another tribe; NPC food,
tool and building snapshots can be rejected by their extra location guards.
Keep native identities local and resolve authoritative identities from the
existing bounded species/position/faction village snapshots. Do not rewrite
live native political IDs or weaken ownership/location checks.

Negative native control: tribal-friendly-mapping-negative.log records host
opinions (1.25,-1.25) and guest (0,1.25) on the same two identified species.
The guest allocated a host NPC's number to the other NPC. Both processes
reported zero native faults and exited normally after the control.

Fixed: bind host IDs to unique local villages by the existing host-reported
species and settlement position; check the live local object and faction
before applying records. Clear mappings on world/session changes. No native
IDs or packet layouts change. Relationship, NPC food, rack and building
snapshots use the mapping in linked campaigns. The performance harness now
uses each peer's actual political IDs for the mirrored chief and performers.

PASS tribal-friendly-mapping-fixture-result.log: host species06785BC2 uses
21D0 while the guest assigns21D0 to067CD01D. Host and guest both report
(1.25,-1.25) for the same two species. The initial replay stopped before any
opinion assertion because its single NPC could not exercise a collision.
The dedicated replay created additional NPCs using the native factory, with
no political-ID assignment; this is not evidence of natural campaign progress.
Windows unit suite passed and both games exited with zero recorded faults.
