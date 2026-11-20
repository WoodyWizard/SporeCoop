---
id: BUG-1339
title: campaign_tc2: the in-game sync-check compares a guest whose own game stands paused: the host's new cities and their vehicles are missing there until it goes on
status: open
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: guest
versions: 0.7.16
fixed_in:
branch: fix/w59-tribe
commits:
tests: campaign_tc2 (gate sync-check); unit net_tests diag digest (stands byte), fuzz_decoders diag Digest
signature: gate sync-check: MISMATCH with player N (Civilization ...): v: 1..3 missing on the guest, that guest's trace busy=paused
docs: docs/06-testing.md#testing-sync-guest-stands
related: BUG-0289, BUG-0335, BUG-0404, BUG-1274, BUG-1323
---

## Report
Lab 20261119-214823 (0.7.16 final check), campaign_tc2 on lane 4 (4 games): the campaign itself `RESULT: OK`, the gate
sync-check failed it: `MISMATCH with player 2 'Carol' (Civilization A3930F03, 2 checks 3 s apart): v: 1 missing on the
guest (v7)` at s 1540 and `v: 3 missing on the guest (v7 v8 v9)` at s 1606. The triage suspected civlook (BUG-1274 own
designs) or the own editors, as the guest's `civ(guest) created=6` stayed flat while the host made new vehicles.

## Root cause
* ✔ (logs) Not civlook, not the own editors: Bob and Dave ran the same own City Hall / vehicle editors (BUG-1276, the
  BUG-1323 resume after each) and the same "shows its own design" lines, and made the new vehicles. Carol's own game
  time stood from 22:59:01 to her rejoin at 23:03:21 (every trace line `busy=paused`, 4.5 min; the campaign itself wrote
  `sync: not compared (paused, an editor or a load): Carol` every 30 s). The host founded two cities meanwhile; Carol's
  game logged `the host founded a city: here founded (cities now 1)` and `the host has 2 cities, this game 1 and 2 founded
  (on their way)` (and 3 later): a paused game makes no city, and the vehicles v7 v8 v9 of those cities have no city
  there (BUG-0335: such a city and its vehicles come when the guest goes on; the campaign's census skips a paused guest
  for that reason). The mod's own sync-check (the lab gate) compared her anyway.
* ? Why Carol's game stood 4.5 min is not in any log: no pause reason is logged, her bot made one noise pause in that
  play (Esc, 2-6 s, Resume Game; BUG-0289 saw a Resume click lost), Bob and Dave one each without a stand. Not the
  BUG-1323 pause (resumed at 22:58:01, the game ran at 22:58:11-22:58:51).

## Fix
* `diag.guest_stands` (diag_sync.cpp): a guest's digest says whether its own game time stands (busy.h kPaused; a trailing
  u8 of MSG_DIAG's digest, session family 1.2: older readers stop before it, an older sender reads 0); the host does not
  compare such a guest (as a guest that plays elsewhere: "not compared until it goes on", once per stretch,
  `stands_skipped=` in `coop.diag sync`). The same rule the campaign's compare and census have. Off: compared.
* Campaign (evidence for the open "why"): a guest that stood through the sync rounds of a minute gets its pause reasons
  (`coop.pauses`), its busy word and its pause menu state logged once; a pause menu still up after 90 s is closed with
  Resume Game as a player would, named in the log.

## Verification
Unit: net_tests (the digest's stands byte round trip, a sender without it reads 0), fuzz_decoders (diag Digest).
Lab: not run yet (the 0.7.16 final check holds the lab): campaign_tc2 again (its gate clean; a `stands` line in the
campaign log names the reason if a guest stands again). No forced twin: a guest's pause during a host's founding is not
made on purpose by any test; `SPORE_FIX_OFF=diag.guest_stands` brings the 0.7.16 compare back.

## History
- 2026-11-20: opened (fix/w59-tribe, triage package G of the 0.7.16 final check): not civlook; a paused guest compared.
- 2026-11-20: diag.guest_stands and the campaign's stand evidence on fix/w59-tribe; lab runs pending.
