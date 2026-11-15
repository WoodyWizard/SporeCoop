---
id: BUG-1091
title: Tests: campaign_tribe_end stuck in the Tribe advance, the host's raid click never selects a member (village 2's hut never falls in 30 min)
status: fixed
kind: test
stage: Tribe
reported: 2026-11-13
source: fix/w49-proxy lab 20261113-181333
role: host
versions: rel/0.7.14
fixed_in: 0.7.14
branch: fix/w49-camp2
commits: 95775cac, b71f4c3a, d18f2a0f
tests: campaign_tribe_noclick, campaign_tribe_end
signature:
docs: docs/06-testing.md#testing-w49-camp2, docs/re/tribe.md#tribe-minimap-map
related: BUG-1073
---

## Report
fix/w49-proxy, campaign_tribe_end lab 20261113-181333 (lane 5, rel/0.7.14 + BUG-1089 / BUG-0440 diagnostics): the
Tribe advance never finished (findings: "[stuck] 18:52:26 tribe/advance: the stage change tribe -> civ did not finish
in 30 min (goal bar (1.0, 5.0))"). From 18:42 every raid click of the host's bot on village 2's hut came back "by []
(the host's click: no member selected (4-7 own))", spawned and ordered members did not fell the hut (750/750) in two
300 s tries. The same build passed the same test on lane 4 (lab 20261113-180327). Same picture as the "no member
selected" of BUG-1073 (cinematics flag 0 in Tribe); the host's trace has `busy=cinematic` in only 4 of 203 Tribe lines
(not checked whether the flag was 0). Not analysed (outside fix/w49-proxy's tickets).
Reproduce: `lab.py run campaign_tribe_end` (1 of 2 runs here).
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
A test-driver problem (the campaign's Tribe advance), not a mod bug, and not BUG-1073.
Observed (the four games' logs of lab 20261113-181333, compared with the passing 20261113-180327 of the same build):
- Not BUG-1073: the host's cinematics were on (the village cinematics of village 1's fall played on the host at
  18:25:55 and 18:26:03 and were ended by BUG-0531's 8 s rule, as in the passing run); no "the cinematics were off after
  the load" line in either run, and the passing run had the same lines. No input lock or pause outside the cinematics.
- The bots gave (almost) no orders in the whole Tribe: the guests' `cmds_sent` stayed 1 (Bob) and 0 (Carol, Dave) from
  18:19 to 18:54; passing runs had 4-11 each. The host's own click on village 1 failed too ("no member selected (4 own)"),
  village 1 fell only through Alice's later step click. On village 2 the host's look failed ("the view did not reach the
  hut", twice, also after a new minimap calibration), then every box found no own member: the host's members had been
  ordered to village 2's hut, where the view did not get. The guests' boxes caught other players' members ("That member
  is Carol's / Those members are Alice's: you lead only your own members").
- Without the host's click the driver never cuts the hut (on purpose: a conquest without the host's mission fills no
  goal, BUG-1068); 15 ordered members left it at 750/750 for 25 min.
- The driver's own loop kept it there: a raid ends after 300 s, and the nearest-first pick (fix/w49-camp) took village 2
  again every time. Its rule "no host click in 3 tries: another village first" counted per raid; one try of
  tribe_clicks took 127-149 s, so a raid had 2-3 tries and never reached 3. Six raids of village 2, 1/5 at the timeout.
Inferred: why the views did not reach village 2 (all four games, the same planet) is not proven. The minimap the bots
click is a fixed map in the planet's own axes, and the part they click (x 15..205, y 940..1025) is a window of ~100 x 55
degrees (runtime probe on the tribe fixture, docs/re/tribe.md#tribe-minimap-map); a target outside it cannot be reached
by one click. Three runs with diagnostics (20261113-190337, -193334, and -200131 with the failing run's seed) did not
hit it: in those the Tribe ended in 13-15 min. Lane 4 passed the same build (20261113-180327).

## Fix
coop_campaign.py (kick_tribe / tribe_clicks / tribe_host_click):
- the host-click tries count per village over all its raids (not per raid), and a raid that ends after 300 s without the
  host's click puts its village aside for 4 min when another one stands ("no attack click of the host on village N in
  its raid: another village first"); the raids go to the other villages and come back later;
- when the host's box finds no own member, it looks at the players' village and boxes its members there (the home guard
  and the members spawned for the click);
- diagnostics when the host's click fails (`BUG-1091 diag:` lines: own members on screen, the view's centre, the bots'
  step counts, screenshots).
`CAMPAIGN_OLD_RAID_SKIP=1`: the old driver. `CAMPAIGN_PLANT_NOCLICK=<min>`: the twin (no bot click on the first village
raided while another stands, for that many minutes). No mod change, no CHANGELOG line (tests only).

## Verification
Suite `campaign_tribe_noclick` (campaign_tribe_end with `CAMPAIGN_PLANT_NOCLICK=18`, lane 5):
- FAIL with `CAMPAIGN_OLD_RAID_SKIP=1`: lab 20261113-212331: village 1 fell, then village 2 (planted) raided 4 times in a
  row for 21 min, 750/750, the Tribe timeout at 3/5 ("stuck"), the ticket's picture.
- PASS with the fix: lab 20261113-220656: after village 2's first raid "another village first", villages 3, 4 and 5
  fell, village 2 put aside again after 3 tries, then fell; the goal bar full, Civ after 1615 s.
- An earlier twin planted the first village raided: 20261113-204040 (old driver) stuck on it, but the game forms the other
  villages only after the first conquest, so that plant cannot tell the drivers apart (changed in d18f2a0f).
- campaign_tribe_end with the diagnostics only (before the fix): 20261113-190337, 20261113-193334, 20261113-200131
  (seed 492354972 of the failing run): the Tribe end came each time (the last failed the lab's sync-check gate, BUG-1082).
- campaign_tribe_end with the fix: 20261113-224609: 5/5 in 12 min, Civ after 869 s, campaign RESULT: OK (the lab failed
  it on the sync-check gate, a tribe member 13 m apart on a guest: BUG-1082's class, not this ticket). The FAIL twin run
  20261113-212331 also failed the ground gate (a host member 8.8 m up for 21 s: BUG-1088's class).

## History
- 2026-11-13: opened.
- 2026-11-13: analysed on fix/w49-camp2: a test-driver problem (the raid loop on a village without the host's click); fixed in the campaign driver, twin campaign_tribe_noclick.
- 2026-11-14: released in 0.7.14.
