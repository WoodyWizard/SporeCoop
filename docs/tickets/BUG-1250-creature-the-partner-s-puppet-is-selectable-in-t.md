---
id: BUG-1250
title: Creature: the partner's puppet is selectable in the social stance; the guest cannot recruit a creature into its pack
status: fixed
kind: bug
stage: Creature
reported: 2026-11-18
source: owner: players' video, second analysis
role: guest
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-puppet
commits: c93944fc
tests: partner_social_hover (tools/tests/coop_partner_social_hover.py)
signature:
docs: docs/re/creature.md#creature-puppet-hunger, docs/re/creature.md#creature-partner-pass, docs/06-testing.md#testing-puppet-after-social
related: BUG-1242, BUG-1256, BUG-0913
---

## Report
A players' video: in the combat stance the partner's puppet could not be clicked; in the social stance it gets the green ring and the name plate, so the players think the partner can be taken into the pack. Then the host recruits a kin creature into its pack, the guest tries the same with another kin, sings for 10 s: nothing ("no, it doesn't work"). Suggested cause: h_HoverFilter (puppets.cpp) passes the partner's puppet only with GetAbilityMode() != 0; social.cpp's apply_success handles only the Impress of other species and has no pack recruit (PosseRecruit) for the guest.

## Root cause
Two parts.
* The ring and the plate (observed, lab 20261118-123440): the Creature mode's hover filter `SporeCoop::CreatureHoverFilter`
  0xD39070 is hooked (puppets.cpp `h_HoverFilter`, BUG-0913) and refused a partner's puppet only in the combat stance
  (GetAbilityMode != 0) and only with friendly fire off. In the social stance the game hovered the host's creature on
  the guest's screen (cGameViewManager +0x88 was the puppet in 12 of 12 samples) and drew its name plate with its health
  and a green face (key frame), though a click on it starts nothing: no social game is made with a puppet (BUG-1241).
* The recruit: a missing feature, not a bug of the hover. A guest's social game is the host's (social.cpp) and its win
  only impresses; there is no pack recruit for a guest. Split into BUG-1256 (static analysis of the native recruit,
  0xD9F7A0 / cPosseSimulator_AddMember, and a design sketch), left open there.

## Fix
The hover filter refuses a partner's puppet in the social stance always (in Creature, connected, outside Rivals), also
with friendly fire on: the hover and the click take the creature behind it. The combat stance keeps BUG-0913's rule.
Switch `coop.fix creature.partner_social_hover off` (0.7.15); `coop.partnerpass` prints `social_hover=` and
`social_hover_skipped=`. The recruit: BUG-1256 (open).

## Verification
Test `partner_social_hover` (creature fixture): the guest's mouse moved onto the host's creature (a real cursor move)
in the social stance; the game's own hover result must never be the puppet.
- FAIL with SPORE_FIX_OFF=creature.partner_social_hover: out/lab/20261118-123440 (lane 5): 12 of 12 samples hover the
  puppet; key frame: the host's creature's name plate with health 10 and a green face on the guest's screen.
- PASS: out/lab/20261118-123633 (lane 5): 0 of 12 (the hover took nothing behind it); key frame without a plate.

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w53-puppet): the hover fixed (creature.partner_social_hover); the recruit split into BUG-1256 (open, design sketch).
