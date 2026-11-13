---
id: BUG-1075
title: Crash at boot (frame 2, no game mode): Simulator::cMissionManager::ChooseMission+0x1043 reads 0x28 (seen once in the lab under load)
status: not-reproduced
kind: crash
stage: Menu
reported: 2026-11-13
source: lab
role: host
versions: rel/0.7.14
fixed_in:
branch: fix/w49-fault
commits:
tests:
signature: SporeApp.exe+0xBEFC03 (0xFEFC03, StarRecord_HasGoldilocksPlanet+0x83; reported as Simulator::cMissionManager::ChooseMission+0x1043) read 0x28
docs: docs/re/space.md#space-star-null-planet-record, docs/06-testing.md#testing-double-instance
related: BUG-0267
---

## Report
fix/w49-tests, lab 20261113-074535 (lane 4, `lab.py run campaign` ran the whole campaign suite by mistake; machine
loaded: lanes 4-7 busy, two other suite starts timed out): campaign_tribe_end's host (instance 7) crashed at frame 2,
game mode none, up 1-2 s, before any co-op session: 0xC0000005 at 0x00FEFC03
`Simulator::cMissionManager::ChooseMission+0x1043` on the main thread, read of 0x28, ebx=0 ebp=0 edx=0. Files:
out/lab/20261113-074535/campaign_tribe_end.crash-20261113-074809-i7.{txt,dmp} (fix/w49-tests worktree).
Seen once; the same test started fine before and after on lane 5.

## Root cause
Observed (Ghidra GOG 2024, the crash report and the logs of lab 20261113-074535):
* The name is wrong: 0xFEFC03 is in `SporeCoop::StarRecord_HasGoldilocksPlanet` 0xFEFB80, not in ChooseMission
  (0xFEEBC0). It loops over a star's planet records (cStarRecord::GetPlanetRecords: the vector read from the save) and
  reads `[record]+0x28` without a null check (`mov edx,[eax]; cmp [edx+0x28],2`): edx = 0, a null pointer inside
  the vector (eax = its data). Chain: ReadStarsFromSaveDB (u"stars.db") -> StarGenerationMessageHandler -> 0xBB2580 ->
  0xBB2350 -> 0xFEFD20 -> 0xFEFB80, main thread, frame 2, game mode none (the profile's galaxy save read at boot).
* The mod is not involved: offline, no session; no hook of ours in the star manager's save read, the planet records
  or the resource reads; the space hooks near it (RequirePlanetsForStar, the RNG, GenerateRandomName,
  cStarRecord_GetSpeciesProfile; BUG-0185 / BUG-1074) log every generation and the crashed game's log has none.
* The crashed game was one of TWO copies of instance 7 started 0.13 s apart (the crash report: pid 448 booted at
  07:48:05.053 and listened on 127.0.0.1:47806; the lab's copy of sporekit.7.log: pid 456 booted at 07:48:05.178, "console:
  cannot listen on 127.0.0.1:47806 (err 10013)"), both with profile 'test', so both read (and may write) the same
  galaxy save at boot. The same run had such doubles at every cold start: campaign (07:45:46, a second instance 7
  pid 624, err 10013), campaign_tribe_end (also instance 8), campaign_space (07:49:52, both instances). Who started
  the second copies was not found (no other lab run on lane 4 then; the ticket's "two other suite starts timed out").
Inferred: a torn read of the shared galaxy save (one copy reading while the other writes) left a planet record
unresolved. A game crash under a broken lab start, not a co-op bug.

## Fix
Not a mod fix. Harness guard (tools/spore.sh run): a second start of an instance that runs, or that was started
under 20 s ago, is refused (exit 3, "not started twice ... BUG-1075") until `spore.sh kill` (the marker
`.started.<inst>` lives in the lane's game folder, shared by every worktree); `SPORE_ALLOW_DOUBLE=1` starts anyway.
tools/tests/lane.py run_host / run_guest print the refusal as a note (the first copy goes on starting), so the
"did not answer: start it again" paths no longer make a second copy. Checked on lane 6: run, run at once (refused:
under 20 s), run after 25 s (refused: runs), kill clears the marker.

## Verification
no-fail-run: not reproduced in 28 cold boots of the tribe fixture's profile to the galaxy menu (the fixture restored
before each, as a cold lab start): 12 on lane 6 under load (8 busy-loop processes, lane 7 running play_tribe and
tribe_away_conquest, lanes 4/5 busy with other agents): 0 faults; 16 with a deliberate double start of the host
instance 0.15 s apart (`SPORE_ALLOW_DOUBLE` behaviour, lanes 6 and 7, 8 each; every one had the second copy's
"cannot listen ... (err 10013)"): 0 crashes, 0 faults, but 3 of 16 never reached the galaxy menu in 120 s (the double
start does harm the boot). Script: the boot loop of fix/w49-fault (scratchpad, not kept). With the guard a lab test
that ran after it: 20261113-134749 (tribe_own_hut_fall, tribe_conquest_guest, cold, lane 6: both OK, no refusal; the starts took 90-98 s under the load).

## History
- 2026-11-13: opened.
- 2026-11-13: analysed on fix/w49-fault: the game's star read at boot in one of two copies of instance 7; not reproduced in 28 boots; harness guard against double starts.
