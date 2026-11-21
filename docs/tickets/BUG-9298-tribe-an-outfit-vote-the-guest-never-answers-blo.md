---
id: BUG-9298
title: Tribe: an outfit vote the guest never answers blocks every save and guest purchase
status: open
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: all
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-9043
---

## Report
Live 67-dev, Tribe. The guest opened its own outfitter (guest 20:31:56); 14 s later the host started the Tribe outfit
vote (host 21:32:13). The guest's outfit result never came, so for 25 minutes (until the guest left at 21:57:17) the host
saved nothing: both "Saving Tribal conquest for your partner..." shares (21:45:19, 21:51:57) were never written, the
10-minute autosave did not run, and 41 guest baby purchases were refused "(your village, or a save running)". The
conquest's checkpoint never reached the guest. Right after the guest left: "Campaign autosaved", "founder outfits saved".

## Root cause
Observed in the logs, cause in the code: `receive_outfit` (tribe.cpp ~3197) drops the vote message unless the game is in
Tribe mode (`active()`), and the guest was in its editor; `vote_pending` (~3157) checks only token, world and that the
player is there, so `outfits_pending()` stays true while the player is connected. `save_blocker` (coop.cpp ~2878) then
answers "Wait for your partner's outfit to finish saving." to every save; `world::save_pending()` stays true and
`buy_guest_baby` / `buy_guest_tool` refuse on it. Alpha has the same code.

## Fix
Planned: the vote ends for a player who has been back in Tribe, out of any editor, for ~30 s (ready with its previous
outfit); a guest opening its outfitter during a vote answers the vote. Switch `tribe.outfit_vote_timeout`. Test: the guest
opens its outfitter, then the host starts the outfit vote: FAIL (outfits_pending stays 1, a share never saves) / PASS.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
