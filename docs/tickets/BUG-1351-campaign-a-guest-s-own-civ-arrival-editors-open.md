---
id: BUG-1351
title: Campaign: a guest's own Civ arrival editors open after the follow phase; the play phase drives them as the spaceship editor and the stage check counts them as apart
status: fixed
kind: test
stage: Civ
reported: 2026-11-20
source: lab
role: 
versions: rel/0.7.16
fixed_in: unreleased
branch: fix/w60-tribe
commits: 56e2f839
tests: campaign_tribe_noclick, campaign_tribe_latecut (and every campaign through Tribe -> Civ)
signature:
docs:
related: BUG-1276, BUG-1277, BUG-1290, BUG-1096, BUG-1328
---

## Report
The 0.7.16 final check (lab 20261120-041304): campaign_tribe_noclick (lane 3) and campaign_tribe_latecut (lane 6) both
"FAIL stage civ/play: Bob / Carol / Dave plays None while the host plays civ", "FAIL apart civ/play: the host does not
see Bob / Carol / Dave in its world (civ)" and "Dave did not get back into the host's world after a rejoin within
300 s (civ)". The campaign log: "[civ/play] Bob: three parts dragged into an empty editor", then "the editor still up
after 6 / 10 "done"" until the end of the stage. `lab.py flaky` calls noclick a known flake (8 of 10 on rel/main since
2026-11-13); the older failures were other causes (Xwayland load, the Tribe advance budget: BUG-1165, BUG-1169).

## Root cause
A test (campaign driver) that does not know 0.7.16's behaviour, not the mod.
* ✔ (guest log 20261120-041304 noclick) WP-X1 (BUG-1276): a guest's own City Hall and vehicle editors open 20 s after
  its game settled in the host's Civ world ("civ arrival: own City Hall editor 99E92F05 requested (20 s after the
  arrival)" at 04:47:23); the host plays on meanwhile.
* ✔ (campaign log) The follow phase ends after the guests are together with the host for 20 s: at 04:47:21, two
  seconds before the editors opened (31 s after the host reached Civ). The play phase began at 04:47:50 with all three
  guests in their editors. The driver (`Campaign.editor`) takes the arrival editors' parts only in the follow phase
  (`civ_arrival = stage == 'civ' and phase == 'follow'`); in the play phase it dragged the spaceship editor's palette
  places (EDITOR_PARTS), which put nothing into the City Hall editor, and pressed "done" 12 times for 5 minutes; the
  guests never left the editor. The stage / apart checks then counted them as apart (an editor is no stage), and Dave's
  rejoin (in its editor) never got back.
* ✔ (campaign log of the PASS run 20261119-214823) there the follow phase lasted longer (Bob's specialty dialogs) and the
  guests' "editor done" came in the follow phase: a race between the 20 s of "together" and the 20 s settle.

## Fix
coop_campaign.py: `arrival_phase` reads the guest's own state (`coop.civarrival` phase=). (1) The follow phase into Civ
counts its 20 s of "together" only once no guest's own arrival editors are due, settling or open (a wait for the
state). (2) `Campaign.editor` drives an editor as an arrival editor whenever the guest's phase says its own arrival
editors are open, not only in the follow phase. (3) The stage and apart checks leave out a guest in its own arrival
editors (the 0.7.16 design: the host is not held), for at most 300 s; longer is its own finding ("still in its own Civ
arrival editors after N s"), so a stuck editor still fails the run.

## Verification
no-fail-run: a test driver fix (the FAILs: lab 20261120-041304 campaign_tribe_noclick and campaign_tribe_latecut).
Was waiting for the lab run: campaign_tribe_noclick and campaign_tribe_latecut must PASS with the guests' "editor
done" lines inside [civ/follow] and no stage / apart finding in civ/play.
- 2026-11-23: Lab on the fix's mod code (rel/0.7.16: 673f7a55 / 21025f40 when the fix was in them, 5376d388 and fd28f88f, whose mod code is the same; 20261122-194210 is the nightly on 5376d388, 20261123-023643 the full run on fd28f88f): campaign_tribe_noclick and campaign_tribe_latecut PASS in 20261120-133703, 20261121-003202, 20261122-194210 and 20261123-071312. Their FAILs in 20261123-023643 had other causes (noclick: Bob's 38 caught faults in tribe/advance, BUG-1460; latecut: the raid with the planted late cut dropped before its hut fell), not the arrival editors. Closed.

## History
- 2026-11-20: opened.
- 2026-11-20: cause and fix on fix/w60-tribe (no lab today, owner rule); lab run pending.
- 2026-11-23: closed on the lab evidence above (0.7.16 release bookkeeping).
