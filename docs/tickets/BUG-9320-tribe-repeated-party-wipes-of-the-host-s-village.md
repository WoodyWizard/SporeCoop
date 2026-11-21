---
id: BUG-9320
title: Tribe: repeated party wipes of the host's village (village down about every 100 s, the hut read 1/400 right after a wipe reload)
status: open
kind: bug
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: host
versions: 0.7.14.1-lineages67-dev (fix/l47-tribe)
fixed_in:
branch: fix/l47-tribe2
commits: db507eef, 8bd69ecc
tests: manual two-game MSI run (Verification)
signature:
docs: docs/re/tribe.md#tribe-wipe-regroup
related: BUG-9155, BUG-9318
switch: tribe.wipe_regroup
---

## Report
Live QA 2026-11-21 (MSI two games, Vandh, Tribe; host MSITribeHost instance 2, guest MSIGuest instance 1), while testing
BUG-9318 with `coop.tribe npc_tribe` villages, `coop.tribe win <id>` test wins (allies) and one `mod.reload coop` on the
host (which ended the session; the host re-hosted and loaded Game0 again). From then on the host's village went down
over and over: host log "Village down. Bring a partner villager ..." at 10:32:45, 10:34:23, 10:36:01, 10:40:12,
10:41:53, 10:43:32 (about every 98 s), "Last villager down" at 10:46:49, each followed 60 s later by "party wipe, seat 0:
Host's village was wiped out: its newest ally (00002296) is a rival again, a totem piece and a step of progress are
lost" and the wipe's reload. `coop.tribe huthp` on the host's hut right after one wipe reload (about 10:41:30): `health
1/400 exact=1.00000 state=1`; after the 10:44:32 wipe, before its reload: 160/400 (the 40 % the rescue sets) and then
376/400 five seconds later (being hit). A crowd of white villagers of the Lavender Village (a native NPC tribe,
archetype 7) stood at the host's hut (out/qa-shots/bug9318_host_native_totem_cutscene.png background).

## Root cause
Found live (fix/l47-tribe2, MSI two games, 2026-11-21; docs/re/tribe.md#tribe-wipe-regroup). The party wipe replaces the
native hut-destroyed defeat, whose OK reloads an earlier save (the hut whole, the raid not there yet). The wipe saves
its checkpoint at the moment of the wipe instead: the raiders stood at the host's hut in that save and went on right
after the reload (`founder huts after this world's load: seat 0 152/400` 3 s after the load), the hut came back at 40 %
(160 of 400, about 30 s of a raid), and the wipe's own rule turns the newest ally into a rival that raids too. So: down
again 30-35 s after each reload, then the 60 s window, then the next wipe: one every 95-110 s, as reported. The "1/400
right after a wipe reload" is that second fall (the hut holds 1 hp during a down window). Candidates (1) (a reload
restoring 1 hp) and (3) (the test set-up) were ruled out: a test blow with no raiders wiped once and the reloaded hut
stayed at 160/400; Vandh's own native villages (archetype 7 and 4, relation -10) raid the host's village with no test
command about 6 minutes after the Tribe entry. Earlier analysis notes, kept: Candidates: (1) a wipe's checkpoint reload restoring the hut at the 1 hp the rescue holds it at (the save
taken while the village was down), so the next blow downs it again at once; (2) the wipe turning the newest ally into
a rival again, which then raids; (3) the test setup itself (test wins, the session restart). Needs a clean repro
without `mod.reload` and test wins: a native raid that downs the host's village once, then watch the hut's health
after the wipe's reload.

## Fix
Switch `tribe.wipe_regroup` (tribe.cpp, on). After a party wipe: the downed village's hut gets its full health (the
native defeat's reload gives a whole hut; 40 % before); once the reloaded world settles (3 s in Tribe), every member of
a non-party village within 90 m of a founder village and more than 25 m from its own village is ordered home
(DoAction 4 to its village), again every 4 s, and for 120 s no blow of a non-party faction harms a founder hut (the
partners load the world in that time). Later raids are ordinary play. Diagnostics (always on): the log names every
village down and last villager down (the blow, the hut's health, the attacker: founder seat, NPC village with archetype,
size and relation, or no village) and every party wipe's reason with every founder hut's health ("party wipe reason"),
and each Tribe load's founder huts ("founder huts after this world's load"). `coop.tribe wins` shows `regroup (BUG-9320)
fix= regroups= blocked= sent_home= left=`. Off: the hut at 40 %, the raid goes on (0.7.14.1).

## Verification
Live, MSI two games (host MSITribeHost instance 2, guest MSIGuest instance 1, Creature -> Tribe from Vandh, fix/l47-tribe2):
- Before the fix (diagnostics build, 12:05-12:15): a test blow alone (`coop.tribe hutfall`, no raiders): one wipe, the
  reloaded hut 160/400 and it stayed there. Then an NPC village (`coop.tribe npc_tribe 3 70`, allied by `coop.tribe
  win`, a rival after the next wipe) ordered to raid the hut (`coop.tribe order <id> 15 <hut> 1E4DAAE`): village down,
  wipe 12:13:07, reload 12:13:24, `founder huts after this world's load: seat 0 152/400`, down again 12:13:56 (31 s after
  the load), wipe again 12:14:56 (out/qa-shots/bug9320_fail_down_again_after_reload.png).
- FAIL with `coop.fix tribe.wipe_regroup off` (fix build, 12:32): the native archetype-7 village's own raid; wipe
  12:32:37, reload 12:32:53, hut 160/400, the raid back at 12:33:31, the last villager down 12:34:38 and the hut down
  12:34:51, the next wipe 12:35:38 (out/qa-shots/bug9320_fail_off_raid_again_after_wipe.png).
- PASS, first version (90 s, one withdraw order), 12:28: a natural raid by the native archetype-7 village (no test
  command) downed the hut 12:28:04; wipe 12:29:04, the hut 400/400 before the save; reload, "regroup ...: 13 raider(s)
  ... walk home", the hut 391/400 through the 90 s window, but the raid came back the moment it ended (12:31:04) and
  downed it 12:31:37 (out/qa-shots/bug9320_native_raid_village_down_host.png). So the order is now repeated every 4 s
  and the window is 120 s.
- PASS, final build (4 s orders, 120 s), 12:52-12:57: a test blow downed the hut (the NPC ally a rival after the wipe),
  wipe 12:53:20, the hut 400/400 at the load, "regroup ...: 1 raider(s) ... walk home"; then 29 raiders of four NPC
  villages were ordered at the hut (12:53:51): none reached it in the window (`coop.tribe wins`: `regroup fix=1
  regroups=1 blocked=0 sent_home=69`), the hut stayed 400/400 to 12:55:50
  (out/qa-shots/bug9320_pass_regroup_hut_whole_no_raiders.png). 10 s after the window the native archetype-7 village
  raided again by itself and downed the hut at 12:56:46 (3 min 7 s after the load; 33 s before the fix).
- Still open: in an idle world next to an aggressive rival the loop goes on, one wipe about every 4 minutes instead
  of every 100 s: the game's raid AI keeps its plan through any number of walk orders. Pausing the raiding village for
  longer (or calming it after a wipe) is a gameplay decision, asked of the owner. No crash report on either game in
  the three sessions.

## History
- 2026-11-21: opened (fix/l47-tribe live QA).
- 2026-11-21: fix/l47-tribe2: root cause found live (the raid survives the wipe's reload, the hut came back at 40 %),
  diagnostics and `tribe.wipe_regroup`; FAIL off / PASS on (Verification).
