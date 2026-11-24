---
id: BUG-9366
title: Tribe: a guest's power buttons show powers it has not earned (the bar of another chief)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-24
source: deep playtest 2026-11-24
role: guest
versions: 
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 0d0214dc
tests: tools/qa_win (manual, TITAN host + MSI guest, a party's Tribe era on a galaxy planet): the guest's power buttons
signature:
docs: docs/re/tribe.md#tribe-power-bar-avatar
related: BUG-9306 BUG-0358
---

## Report
Deep playtest 2026-11-24 (a party crossed from Creature into Tribe on a galaxy planet): the host's two power
buttons were Traps and Fire Bombs and worked (a trap placed, the recharge shown). The guest's two buttons read
"Refreshing Storm [F1]" and a second power it has not earned; a press said "Play the Creature Stage to discover how
unlock this ability!" (images/bug9366_guest_power_bar_off.png). `coop.tribe powers` on the guest showed the same
unlock word as the host's (0x00900000: 0x34 and 0x37 usable) and `coop.tribe powers use 0x34` worked (the host ran it,
a trap at the guest's village): the buttons were wrong, not the powers.

## Root cause
Observed (`coop.tribe powers` on both games) and read (the .sym notes of AbilityBar_Rebuild 0xD49400): the HUD's
ability bar holds ability indices of the avatar it was built for at the HUD's start, in Tribe the player tribe's
chief. The host's chief had power 0x34 at index 8 of 14 abilities; the guest's own chief (its own creature) at index
7 of 13. The guest's player tribe becomes its own village after the HUD's start, so its bar's slots (8 and 11) named
its own chief's abilities one index on (0x35 and 0x38), which it has not earned. BUG-0358's rebuild covers the
Creature stage only.

## Fix
tribe.cpp power_bar_tick: in Tribe the bar is built again (AbilityBar_Rebuild, outside the frame) whenever the
player tribe's chief, the index of its first power or its ability count changes. Switch `coop.fix tribe.power_bar
off`: the bar of the HUD's start.

## Verification
- FAIL (build ffd01832, 15:47): images/bug9366_guest_power_bar_off.png.
- PASS (the fix, 15:58, the same Tribe era entered again): the guest's log "tribe: the power bar is built again for
  chief 22BD5C90 (its powers from ability 7 of 13, BUG-9366)", its buttons read "Traps [F1]" and "Fire Bombs [F2]"
  (images/bug9366_guest_power_bar_on.png).
- Not run: a real click on the guest's Traps after the fix (it was recharging from the test command's use).
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the picture of the build before.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the deep playtest).
