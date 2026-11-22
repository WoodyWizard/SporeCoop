---
id: BUG-9300
title: Tribe (guest): the member list shows both players' villagers after the own-village swap
status: fixed
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-view
commits: 7c53547a, 51fbab3d, ab158f07
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
signature:
docs: docs/re/tribe.md#tribe-swap-cards
related: BUG-9153
---

## Report
Live 67-dev, Tribe: "The Pack list showed both party member's units, rather than just the player's." (the guest's
member cards).

## Root cause
Likely (code + log, not seen on screen yet): the guest's world load builds the member cards while the host's village is
still the player tribe; BUG-9153's swap (`persp_update` / `persp_restore`, tribe.cpp ~2827-2859, SetPlayerTribe) then
makes the own village the player's, which adds its members' cards, and nothing removes the cards of the village that
stopped being the player's (cards leave only through TribeStage_RemoveMemberCard, docs/re/tribe.md#tribe-roster-cards).
The guest swapped and restored many times this session (20:26:40, :50, :53.8, 20:27:01, 20:38:36; restores at 20:26:47,
20:26:53, 20:31:56).

CORRECTION / precision (2026-11-20, fix/l47-view): not seen in a game by this branch. Observed in the code: nothing in
the mod removes a card on the swap or the restore (TribeStage_RemoveMemberCard is called only by detach_body), and the
card count is readable (TribeGameUI +0xA8). Inferred, not checked: how the game adds the new player tribe's cards on a
SetPlayerTribe (the MSI saw the cards become the own villagers on 2026-11-12, so something adds them; whether it also
removes the old ones in some cases is unknown; no Ghidra / running game on this PC).

## Fix
Switch `tribe.swap_cards` (tribe.cpp, next to persp_restore; on). On a guest in Tribe, twice a second
(swap_cards_tick, from persp_update): when the roster has more cards (TribeGameUI +0xA8) than the game's player tribe
(native_player_tribe) has members, every other tribe's members lose their card (TribeStage_RemoveMemberCard, no
card: nothing). On persp_restore the village that stops being the player's loses its cards at once (the host's come
back by the same game path that added the own ones). body_card_off is split: roster_card_off (no switch) +
body_card_off (`tribe.body_card` as before). Readout: `coop.tribe perspective` adds `cards= player_members= cards_off=
card_passes=`; the log line "the member cards of villages that are not the player's went: A -> B".
Test (manual, two players): cards == player_members on the guest after the swap, after a save (restore + re-swap)
and after the host leaves to the menu and comes back; off: cards = own + host members.
## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): PASS (on): guest cards=3 player_members=3 cards_off=9 (first run) and cards_off=3 (second); the host (after ab158f07) cards=3 player_members=3 cards_off=6. The first run (before ab158f07) showed the guest's 2 villager cards on the host's roster. Off-run not done.
no-fail-run: checked by hand in live two-game QA (no lab on Windows); the PASS evidence is above, the old behaviour stays behind the switch.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-view (built, not run in a game yet; the lead runs the two-player test).
- 2026-11-21: live MSI (two games, merged into feat/lineages-47): PASS on the guest (`cards=3 player_members=3
  cards_off=9`), but the HOST's roster showed 2 cards of the guest's villagers next to its own 3 after a fresh
  Creature -> Tribe entry with per-player founding. The pass now runs on the host too (the same swap_cards_tick, still
  `tribe.swap_cards`; `coop.tribe perspective` reads `cards=` / `player_members=` on the host as well). Where the host's
  extra cards come from is not known (likely, not checked: the founding's SpawnNpcTribe(.., 3 members, ..) telling the
  stage of new members; no card-add function is known, so it is not stopped at the source).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
