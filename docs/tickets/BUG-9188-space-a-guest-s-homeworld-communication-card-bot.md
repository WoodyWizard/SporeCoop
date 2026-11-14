---
id: BUG-9188
title: Space: a guest's homeworld communication card (bottom left) stays blank after a personal launch
status: fixed
kind: bug
stage: space
reported: 2026-11-13
source: test
role: guest
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 72bc723c
tests: tools/qa_win (two TITAN games by hand: DuoQAJiffSpace host, a guest joined in Space, the switch off and on, screenshots)
signature:
docs: docs/re/space.md#space-guest-species-card
related: BUG-9187 BUG-9191
---

## Report
Seen in the four-founder launch (2026-11-13): on each guest (Late, Fourth, the MSI) the homeworld communication card next to the minimap ("Gentmin E...") is a blank white picture, still blank after the welcome captions (out/shots/guest-space-blank-card.png); the host's shows its species (out/shots/host-space-card.png). Cosmetic.

## Root cause
Observed (2026-11-14, two TITAN games): the card's image window (control 0x036C0957 under 0x05D646C8) has an ImageDrawable whose image (+0x28) is 128x128 with a texture on the host and 1x1 with none on the guest: the load found nothing. The host's own creature (291E99C5, 7D75B721 on the guest) reaches a guest as its model and metadata only (kOwnSpecies, records_from_game takes no png). Registering the png in the creature group + 1 (the plain thumbnail) alone did not fill it; in the creature group (0x40626200, the Sporepedia card) it did.

## Fix
space.species_card (on): on a guest, the host's creature picture (BUG-9191's Party panel picture, already sent at the join) is registered as that species' png (0x2F7D0004) in the creature group and group + 1, under the host's key and the guest's own key of it (creations.cpp species_card), whichever of the picture and the species arrives last. No metadata goes with it, so no Sporepedia entry. No new network traffic.

## Verification
Live, two TITAN QA games (2026-11-14, host DuoQAJiffSpace with "Jiff Peanutbutter.spo" in Space, guest DuoQACreatureGuest joined there, 1024x768): FAIL (the build without the fix, twice, and `coop.fix space.species_card off` on the guest before the join): the card blank white; PASS (on): the card shows the host's creature, as on the host ("registered as the thumbnail of 291E99C5 and 7D75B721" in the guest's log). no-fail-run: not a lab run (TITAN QA games, by hand).

## History
- 2026-11-13: opened.
- 2026-11-14: fixed (the host's picture as the species' png on guests), live-tested with two games.
