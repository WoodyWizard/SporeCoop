---
id: BUG-9302
title: Tribe: a partner's villagers carry no mark in that player's colour
status: open
kind: feature
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: all
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-view
commits:
tests: manual two-player plan (ticket Fix)
signature:
docs: docs/re/tribe.md#tribe-swap-cards
related: BUG-9282
---

## Report
Live 67-dev, Tribe: "Party's creatures were not highlighted the party color, so they were hard to tell who was who."

## Root cause
Feature gap: BUG-9282 colours only the partner's name plate; nothing marks tribe members on Live or alpha. Village
identity colours (cTribe +0x234) are the game's own forming-order colours and tint only huts and flags.

Decision 7K A (owner 2026-11-20): a small ring under each partner villager's feet in that player's colour.
Existing helpers checked: the game's own target ring (TribeTargetRing_Draw 0xCCBE90) is an effect with fixed colours
(kind 0..3), so not used; ring_timer / partner_marker draw screen-space shapes with D3D9 (XYZRHW); the new ring reuses
partner_marker's flat alpha-blended state and projects a ground circle point by point with world_to_screen.

## Fix
Switch `tribe.partner_ring` (coop.cpp, before draw_civ_tags; on). Each overlay frame in Tribe with labels shown (not
under the menu or a coop window), draw_partner_rings walks tribe::for_each_tracked (host: g_hm, guest: the snapshot's
owners, every village) and, for every live member whose owner is a partner (not this player, < 0xFE), draws
partner_marker::ground_rings: a 24-segment circle on the planet's tangent plane at the member's position (0.12 m up),
radius 0.7 x the bounding radius clamped to 0.5..1.8 m, a band 0.78..1.0 r in 0xE6 + color_of(owner) (the name
plate's colour, 7D A) over a dark band 0.68..1.1 r; a ring with a point behind the camera is left out. Drawn without
depth: over the villager's legs where they stand in front. Readout: `coop.status` line `labels: ... partner_rings=N`.
Test (manual): both games show rings under the other player's villagers only, in that player's plate colour; off:
partner_rings=0 and no ring.
## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): partial: host `coop.status` partner_rings=3, guest 0 (the host village was off its screen); the rings were not seen in a screenshot (the test camera never reached the partner village).

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): PASS (seen on both screens). Host camera on the guest's village (minimap
  click): blue rings under the guest's three villagers, the MSI card's colour (out/qa-shots/bug9302_host_rings_on_guest_villagers.png);
  guest camera on the host's village (arrow keys): yellow rings under "Host" / "Host's chief" in the host's plate colour
  (out/qa-shots/bug9302_guest_rings_on_host_villagers.png). `coop.status` partner_rings=3 on each. No ring under the own
  villagers. Off-run not done. (Screenshots in the worktree's out/, not in git.)

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-view (built, not run in a game yet; the lead runs the two-player test).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live QA run (see Verification).
- 2026-11-21: the ring's look and stages were extended by BUG-9321 (soft, depth-tested, every stage); `tribe.partner_ring` still turns the Tribe rings off.
