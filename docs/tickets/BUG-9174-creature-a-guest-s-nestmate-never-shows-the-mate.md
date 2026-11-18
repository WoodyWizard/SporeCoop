---
id: BUG-9174
title: Creature: a guest's nestmate never shows the mate icon or hearts (the game offers no copy as mate)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, the MSI as a guest of the TITAN host 2026-11-13: coop.evolve mates, the mating call button, a click on the offered copy, the switch off and on)
signature:
docs: docs/re/creature.md#creature-mate-keeper
related: BUG-9169 BUG-9160 BUG-9161 BUG-0177
switch: creature.guest_mate_offer
---

## Report
The owner (2026-11-12 and 2026-11-13, the MSI as a guest): the nestmate shows the happy face, not the mate icon, and no hearts (screenshot: the card "Lilku" with the smiley).

## Root cause
Observed (disassembly, Steam 3010a413): CreatureClickAction (0xD30830) -> 0xD2FFD0 returns 9 (mate) only for a creature with +0xB58 bit 0x10. That bit is set by the Creature game's mate keeper 0xD52270 (every frame): it keeps one nestmate in its slot +0x38 (an intrusive pointer), picked from the avatar's family list (+0x1124, checked by 0xD4B2A0: grown +0xB34 == 1, the avatar's species +0xB20, +0x135 set, +0xB5E clear, ...), and marks it (+0xB58 |= 0x10, controller +0xB4C +0x5FC |= 0x4000); none while the avatar's +0x1678 > 0. A guest's family are copies of the host's members (npc.cpp), never in that list: the slot stayed empty, all copies class 7. BUG-9169 had already given the copies the own species (a mate must have it).

## Fix
creature.guest_mate_offer (on), coop.cpp mate_keeper (a hook after 0xD52270): on a guest in Creature with an empty slot (and +0x1678 <= 0), twice a second the nearest grown copy of the own species in the own nest herd (40 m of the nest, with a controller: the game's clear 0xD4B180 reads it unchecked) goes into the slot through IntrusivePtr_Assign and gets both bits, as the game marks its own offer; the game validates and clears it itself afterwards; a copy the game cleared within 2 s of its offer (its own check refuses it: the TITAN guest, the same copy offered 190 times in 95 s) is left out for 30 s. mate_hearts_tick gives such a copy the hearts. `coop.evolve mates` prints the keeper, the slot and the copies offered.

## Verification
no-fail-run: manual on two PCs (no Windows lab lane). FAIL (the switch off, a fresh join): "mate keeper 1FF202A0 slot 00000000", three own-family copies class 7 hearts 0. PASS (on): "slot 38C5CC30 ... copies offered 3", that copy class 9 hearts 1 (screenshot: hearts over the small nestmate). The hover, with the real mouse (owner: "hover over the mate with my mouse, not your private one"; tools/qa_win/hover.ps1, 2026-11-13 on TITAN): the game's own mate on the host: the cursor an arrow with hearts, the card the green smiley, also during the mating call (CORRECTION, later that day: in this world, where the game's player species was not the creature played; with it right the card shows the pink mate face: BUG-9177); the guest's offered copy: the same cursor with hearts and the smiley card (out/shots/mate-hover-host-vs-guest.png). CORRECTION (2026-11-13): the earlier hover tries used the console's `move`, a virtual cursor that lasts 5 s (sporekit/focus.cpp), not the real pointer. Open, BUG-9175: a click on the offered copy starts the native mating (action 02D852F0, hearts on the player and the family) and it stalls (no animal target, mating step state 0 -1 -256 0 for minutes).

## History
- 2026-11-13: opened.
- 2026-11-13: the hover checked with the real mouse on TITAN (host reference and guest); refused candidates skipped.
- 2026-11-13 (later): the card's face is BUG-9177 (the game's player species), not the mate mark; the 40 m limit round the nest dropped (a lone member 73 m out was never offered on the MSI).
