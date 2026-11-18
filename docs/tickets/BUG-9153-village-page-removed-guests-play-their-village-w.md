---
id: BUG-9153
title: Village page removed: guests play their village with the game's own controls
status: open
kind: feature
stage: tribe
reported: 2026-11-12
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual two-PC run 2026-11-12)
signature: MSI crash 20261112-140751: read of 0x154 at Tribe_HutStateIs2 (C90730) +C via the getter 8E8230
docs: docs/re/tribe.md#tribe-guest-own-player-village
related: BUG-9152 BUG-9135
switch: ui.village_page, tribe.own_player_village
---

## Report
Owner (2026-11-12, decision 2L): "B - prioritize this": the Co-op window's "Village >" page goes now; a guest plays their own village with the game's own controls. QA on the MSI (guest): the guest's HUD shows the host's village as its own (the food counter, the villager cards on the right, the camera's home), so the hut's Add Baby, the tool menu and Rebuild only existed as Village page buttons.

## Root cause
Spore has one player tribe per game (noun manager +0x70); on a guest's game it is the host's village and the guest's own village is an NPC tribe there. Every native player UI (food counter, member cards, hut panel, tool planner, camera home) reads the player tribe. Observed 2026-11-12 on the MSI: with SetPlayerTribe pointed at the guest's own village (`coop.tribe perspective own`) the food counter and the cards follow it and a card click selects the guest's chief; the camera stays at the host's village. The first try crashed about 15 s later: the Tribe UI's ring code (TribalGame_HandleMessage message type 3 -> CD5190 -> CCC580 -> CC95E0) called Tribe_HutStateIs2 (C90730) for the owner of a citizen whose hut (+0x368) was null; C90730 has no null check. Which tribe had no hut is not known (both villages had one).

## Fix
Step 1 (this commit): the Village page is hidden (`coop.fix ui.village_page on`: back). Its console commands stay for tests. Until the native versions come, a guest cannot add a baby, build a tool or rebuild a partner's village (owner accepted that with 2L B). Tribe_HutStateIs2 gets a null-hut guard (false). The experiment `coop.tribe perspective own|host|status` swaps a guest's player tribe; player_tribe() keeps meaning the host's village for the mod while swapped. Next: the swap as the guest's normal state (join, stage and world changes, leave), the camera home, the hut's Add Baby and the tool planner sent to the host (CMD_BABY / CMD_BUILD exist), Rebuild automatic with the party wipe (decision 1U).

## Verification
no-fail-run: feature in progress, manual two-PC run. 2026-11-12, TITAN host + MSI guest on a fresh copy of the owner's Tribe save: the swap without the guard crashed the MSI after about 15 s (crash-20261112-140751); with the guard it ran over 6 minutes with 0 faults (hutless reads 0: the trigger did not come again). The Village button is gone from the Co-op window.

## Fix, step 2 (2026-11-12)
`tribe.own_player_village` (on): a guest's game makes its own village the player tribe by itself (persp_update, every
tick) and puts the host's village back for a save, the session's end and the mod's unload, the host away (local play)
and when the own village goes; a world change forgets it (the game's teardown let go). The hut's Add Baby
(TribeHutUI_HandleMessage CDBEA0, button 0625CD52) on a guest goes to the host as CMD_BABY. A rack the guest places
with the game's Tribal Planner is sent to the host as CMD_BUILD when the host's tool list lacks it (guest_tools: the
planner leaves the rack marked +0x84 bit 8 on the guest even after it closes; civ::planner_open() tells the open
planner apart), then the host's rack replaces it; a refused one goes after 4 s. Refusals of a guest's baby or rack are
sent to that player (MSG_NOTICE) and logged on the host; before, the host read them.

Verification, step 2: no-fail-run (feature, manual two-PC run). 2026-11-12, TITAN host + MSI guest, fresh copy of the
owner's Tribe save, real clicks on the MSI: the swap came on at join and after a mod reload, a save (restored and back)
and two world reloads (log lines "the host's village is the player's again (session end / save)"); the hut menu
"Clop Village" opened for the guest's village; Add Baby made a baby on both games (1 -> 2 members, host cmds_done 1);
the Tribal Planner opened with the guest's food; Wooden Horns dropped on a lit spot and the planner closed: the host
built it ("Your partner built a tool rack in their village", 25 food taken), the guest's rack became the host's (flags
0x8 -> 0x0). The first try before the sync change was refused by the host and the reason went only to the host's
screen (now fixed). 0 faults on both games over the run.

Still open: the camera starts at the host's village (Home and the minimap do not move it to the own one), Rebuild
(automatic with the party wipe, decision 1U), the other right-click actions not seen live one by one (raid, gather,
equip, heal, eat, gift, perform), moving a placed rack (it goes back), the Tribe -> Civ graduation and the outfit
editor with the swap on.

## History
- 2026-11-12: opened.
- 2026-11-12: step 2, the guest's own village is its player tribe; Add Baby and the Tribal Planner go to the host.
