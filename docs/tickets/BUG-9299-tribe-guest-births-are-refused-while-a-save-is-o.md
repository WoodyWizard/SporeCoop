---
id: BUG-9299
title: Tribe (guest): births are refused while a save is only queued or the host's own birth is pending
status: open
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-births
commits:
tests:
signature:
docs: docs/re/tribe.md#tribe-outfit-vote-timeout-births, docs/06-testing.md#testing-outfit-vote-timeout-births
related: BUG-9298
---

## Report
Live 67-dev, Tribe: "Adding babies seemed inconsistent and broke a lot of the time." Host log: one guest baby at 21:31:38,
then five refusals "Births are not available at this point in the Tribal campaign." (21:31:39-47); two babies at 21:39:06;
41 refusals "The host could not add that baby just now (your village, or a save running)" from 21:46:39 to 21:56:50.

## Root cause
1. The 41 refusals: `world::save_pending()` in `buy_guest_baby` (tribe.cpp ~8566) while the stuck outfit vote held a share
   queued (the outfit vote ticket opened with this one). A queued share has not started writing; the purchase would be in it.
2. The five refusals (likely, not proven at runtime): `!F<uint8_t>(ui, 0x3A3)` reads the HOST's TribalGame
   mbMatingAllowed (docs/re/tribe.md), cleared while the host's own birth is on its way (the host's food fell 49 -> 29 ->
   19 in that window: the host was buying babies). The guest's births depend on the host's own birth state.
   The guest's purchase does not use the native birth (SpawnMember on its own village), so the flag only says the
   host's single native birth is busy for a few seconds (TribeStage_BabyBorn sets it to 1 when that baby comes).
3. The guest sends one order per click with no in-flight guard (`h_HutUI` ~2870) and the reasons are lumped into one
   message, so a player clicks 5-14 times and learns nothing.

## Fix
Planned: drop `save_pending()` (keep `save_busy()` and `load_queued()`) in buy_guest_baby and buy_guest_tool; skip the
host's +0x3A3 flag for a guest purchase (keep BirthBlocked) after logging its value once; separate refusal messages; a
0.5 s in-flight guard on the guest. Switch `tribe.guest_birth_gate`. Test duo_tribe_birth.py with a queued share and a
host birth in flight: FAIL / PASS.

Done (2026-11-20, fix/l47-births), switch `tribe.guest_birth_gate` (on; off = refused at once with the old message):
- `save_pending()` (a share's save only queued) no longer refuses a birth or a rack: the purchase goes into that save.
- A save being written (`save_busy`), a load (`load_queued`) or the host's +0x3A3 down HOLD the birth instead of
  refusing it (not skipped, as first planned: the flag is also the native unlock, so a held birth respects it): the host
  tries it again every 0.5 s for at most 20 s (`guest_birth_tick`), one held birth per player (a further order: "Your last
  baby is still waiting ..."). The buyer is told "Your baby comes in a few seconds: <reason>."; after 20 s "The host could
  not add that baby: <reason> for 20 seconds. Try again." The hold logs the +3A3 value (evidence for cause 2).
- Separate messages: village / villager not found, births not available (BirthBlocked), food, the population limit, the
  wait reasons; racks: "saving or loading" apart from "village or villager not found".
- Guest: one birth order per 0.5 s (the native hut button and `coop.tribe baby`); `coop.tribe births` shows
  `waiting held after_wait expired clicks_guarded rejected`.
## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): partial: a guest baby with food: "A baby joined your partner's original species village."; without food two clear refusals ("Your village needs more food to add a baby."); a held birth during a save or the host's own birth was not reproduced.

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): PASS for the save case: host `coop.save` and guest `coop.tribe baby` sent in the
  same moment: host 04:21:07.907 "player 1's baby waits up to 20 s: the host's game is saving (host birth flag +3A3=1, save
  queued=0 busy=1, load=0)", guest "Your baby comes in a few seconds: the host's game is saving.", host 04:21:10.497 "held
  baby added after 2.6 s", guest "A baby joined ...". Not reached: the host's own hut birth then the guest's within 5 s
  (needs the host's hut UI); off-run not done.

## History
- 2026-11-20: opened.
- 2026-11-20: fixed on fix/l47-births (`tribe.guest_birth_gate`); built, not run in a game yet (the lead's two-player test).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live QA run (see Verification).
