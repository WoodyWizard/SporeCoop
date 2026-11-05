---
id: BUG-0574
title: Tribe: two buildings stand right next to each other on the guest
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.6
branch: fix/w42-tribe-left
commits: 97b06246 748cf8ed
tests: tribe_tools_all (coop_tribe_tools_all.py)
signature:
docs: docs/re/tribe.md#tribe-tools-all
related: BUG-0523
switch: wobj.tribe_tools_all
---

## Report
The guest: literally two buildings next to each other in Tribe (a picture sent to the developer).
Reports: SporeCoop-report-2026-11-04 200959 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s9694: the guest's village has its tool buildings at other places than the host's (two red-roofed huts close together, a totem missing); the host's has a totem, two huts at the bottom. Compare BUG-0523 (each game places the tools on its own, fixed in 0.7.6).
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed: the report is on 0.7.5; s 9694 shows the guest's tool buildings at other places than the host's,
  a totem missing: each game placed the tools on its own, the bug fixed by BUG-0523 in 0.7.6
  (wobj.tribe_tools_all: every tribe's tools host-owned world objects, a guest's own copies removed).
* Lab on 0.7.6 code: tribe_tools_all PASS out/lab/20261104-200546; the same test with
  SPORE_FIX_OFF=wobj.tribe_tools_all FAIL out/lab/20261104-201624 (the guest keeps its own tool 13 m from the
  host's rack: "two buildings next to each other").

## Fix
Fixed by BUG-0523 (0.7.6, `wobj.tribe_tools_all`): see that ticket.

## Verification
`coop_tribe_tools_all.py` (suite `tribe_tools_all`) replays the situation: an NPC tribe's tool the host places,
a guest's own tool of that tribe elsewhere; checked in the guest's game (its tribes' own tool lists).
* FAIL `SPORE_FIX_OFF=wobj.tribe_tools_all`: out/lab/20261104-201624.
* PASS (0.7.6 code, rel/0.7.7 head 07e04b98): out/lab/20261104-200546.
Not verified: the player tribe's tools after the host's reload (tribe_bounce_world, BUG-0564, does not compare tools).

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; fixed by BUG-0523 (0.7.6): tribe_tools_all PASS out/lab/20261104-200546, FAIL with the switch off out/lab/20261104-201624.
- 2026-11-04 (fix/w43-detector2, D0 detector): `Tribe:planet:tribe_tool` in 41 % of the Tribe comparisons: an NPC tribe's tools at other spots on the two planets (e.g. host only at (335 -136 353), guest only at (330 -175 341)), from the first sample of a fresh session on, labs 20261104-201842 / 20261104-203017 / 20261104-210208.
