---
id: BUG-0752
title: Creature: a standing NPC proxy pressed 0.5-1.3 m into the ground after the host's evolution editor stays there for over a minute (campaign_ct monitor gate proxies)
status: open
kind: desync
stage: Creature
reported: 2026-11-08
source: rel/0.7.11 fa4f5df3 labs 20261108-072547, 20261108-075905-4
role: guest
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
campaign_ct on rel/0.7.11 fa4f5df3 failed 2 of 3 on the monitor gate `proxies: guest: 1 NPC proxies under the ground
(Creature) on 2 lines from s 430/440` (out/lab/20261108-072547 lane 4, 20261108-075905-4 lane 6; passed 20261108-075905
lane 7), right after the host's evolution editor.

## Root cause
Observed: docs/re/creature.md#creature-proxy-ground (new proxies of a wild herd made on the ground after the host's
editor, under it 1-2 s later, one standing 0.52-0.55 m under for over 70 s). The Tribe case BUG-0399 in Creature: the
standing proxy's `ground` rule of drive::step was Tribe only. Not a K4 / K2b / K7 switch (campaign_ct 3 of 3 with
SPORE_FIX_OFF=drive.lead_fall and 3 of 3 with all on, lab 20261108-081330 / -2). Why it began to show on rel/0.7.11 is not
known.

## Fix
`coop.fix npc.proxy_ground_creature` (npc.cpp): a standing proxy in the Creature stage gets drive::step's `ground` (more
than 0.25 m under this game's terrain: put onto it, the target never under it), as Tribe proxies have had since BUG-0399.
Off: Tribe only (0.7.10).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-08: opened.
