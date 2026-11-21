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
branch: fix/l47-births
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-outfit-vote-timeout-births, docs/06-testing.md#testing-outfit-vote-timeout-births
related: BUG-9043, BUG-9299
---

## Report
Live 67-dev, Tribe. The guest opened its own outfitter (guest 20:31:56); 14 s later the host started the Tribe outfit
vote (host 21:32:13). The guest's outfit result never came, so for 25 minutes (until the guest left at 21:57:17) the host
saved nothing: both "Saving Tribal conquest for your partner..." shares (21:45:19, 21:51:57) were never written, the
10-minute autosave did not run, and 41 guest baby purchases were refused "(your village, or a save running)". The
conquest's checkpoint never reached the guest. Right after the guest left: "Campaign autosaved", "founder outfits saved".

## Root cause
Observed in the logs (guest 20:31:56 "evolves its own creature", Tribe -> Editor; 20:32:10 the vote, editor 156276D1,
started and ended while the guest stayed in its editor until 20:38:36; no outfit result in either log), cause in the
code: `receive_outfit` (tribe.cpp ~3197) drops the vote message unless the game is in
Tribe mode (`active()`), and the guest was in its editor; `vote_pending` (~3157) checks only token, world and that the
player is there, so `outfits_pending()` stays true while the player is connected. `save_blocker` (coop.cpp ~2878) then
answers "Wait for your partner's outfit to finish saving." to every save; `world::save_pending()` stays true and
`buy_guest_baby` / `buy_guest_tool` refuse on it. Alpha has the same code.

## Fix
Planned: the vote ends for a player who has been back in Tribe, out of any editor, for ~30 s (ready with its previous
outfit); a guest opening its outfitter during a vote answers the vote. Switch `tribe.outfit_vote_timeout`. Test: the guest
opens its outfitter, then the host starts the outfit vote: FAIL (outfits_pending stays 1, a share never saves) / PASS.

Done (2026-11-20, fix/l47-births), switch `tribe.outfit_vote_timeout` (on; off = the old wait):
- Host (`outfit_vote_timeouts`, run first in `outfit_tick`): a vote still waiting for an answer (not ready, or the host
  never entered its editor) ends when its player has been out of any editor for 90 s while the host plays Tribe. The
  player's mode comes from its latest PlayerState (new `own_world::peer_mode`); the clock restarts while the player is in
  an editor and after any gap in the host's Tribe tick (the host in its own outfitter). The vote is cleared (`vote = {}`):
  that village keeps its outfit, the player gets "Your new outfit did not reach the host in time ...", the log says
  `outfit vote ... ended unanswered`. Saves and the queued share go on (`outfits_pending()` 0).
- 90 s, not the planned 30 s: a guest's voted outfitter may wait up to 90 s for its stage to settle (own_editor_tick)
  and its result is read up to 30 s after it closes (creations' adoption window); 30 s could drop a real answer.
- Guest (`receive_outfit`): the vote's start is also kept while the guest is in an editor of the same world, so an
  outfitter opened through `open_own_editor` answers the vote on its return. The editor the game opens by itself (the
  report's case) never reaches `finish_own_outfit` (inferred from the code), so there only the host's timeout ends it.
- `coop.tribe outfits` shows `idle=` (seconds counted) and `timed_out=`.
- Not changed: a vote whose answer came and whose result fails to apply every time (`Outfit not applied: ...`) still holds
  saves (not seen in a report).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): not run (needs the outfitter vote with both editors).

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): PASS (switch on). The guest opened its own editor (`coop.vote editor 156276D1`),
  then the host started the outfit vote (`coop.vote editor 156276D1`, "ready 1/2"), pressed Start Now and finished its
  outfitter (one part added); `coop.tribe outfits`: token=1 ready=0 host_entered=1 pending=1. The guest left its editor at
  04:16:50; idle counted (18 s at 04:17:10); host 04:18:23 "seat 1's outfit vote (token 1, ...) ended unanswered 90 s
  after the player left any editor" (93 s); then token=0 pending=0 timed_out=1, an event autosave 2 s later ("campaign
  autosave confirmed") and `coop.save` "saved". Off-run not done.

- Live QA 2026-11-21, fix/l47-final-tribe (MSI two games, Vandh, Tribe): the answered case, twice. (1) `coop.vote
  editor` on the host, the guest's I'm Ready click in the lobby, both outfitters, the guest's Done first: `token=1
  pending=1 idle=` counted while the guest was back in Tribe, then the host's Done ended the vote (`token=0 pending=0
  timed_out=0`). (2) The host's Planner -> Edit Outfit, the guest's Done in its own outfitter: `ready=1
  result=7CE63A2C`, then `token=0 pending=0`, the host's new outfit recorded and shared (BUG-9301). No vote was left
  pending, no timeout needed.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on fix/l47-births (`tribe.outfit_vote_timeout`); built, not run in a game yet (the lead's two-player test).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live QA run (see Verification).
- 2026-11-21: fix/l47-final-tribe: the answered vote ends normally, twice (Verification).
