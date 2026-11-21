---
id: BUG-9303
title: Tribe (guest): orders the host does not allow are dropped silently (fishing, meat, eggs, bundles, loose fruit, repair) and the chief aura is host-only
status: open
kind: bug
stage: tribe
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Live 67-dev, Tribe: "Firebombs did not work ... I assume nearly all powers are going to be broken now. We need a deep
look into this." The host's `rejected=` counter rose to 147 with no log line (41 were baby refusals; 45 came in 40 s
after "epic creature down").

## Root cause
Observed in the code (host_command's allowlist, tribe.cpp ~5450-5460): a guest's fishing (1), carcass meat (27), eggs
(13), bundles (14), raiding a food mat (8), repair (24), tame/feed (20/25) are not allowed; a right-click on loose fruit
(2 with kind 2) is rejected before the fruit fallback; ~40 rejection places build a reason in g_last_cmd and never log it.
`chiefs_tick` (~7249) scans only the host's player tribe, so a guest's chief gives no aura and no rescue.
Firebombs (Flaming Torches raiding huts): the raid order (15) is allowed, but no member of the guest ever held torches in
this session (all tool=3 spears; one rack built), so torches were likely never unlocked for the guest, or the conquest's
unlock never reached it (the stuck outfit vote ticket). Needs a repro with a torch rack.

## Fix
Planned: log every rejection (rate-limited); allow the missing actions with a target check like hunt's; accept
loose-fruit gathers; chiefs_tick walks every founder village. Torches: a repro with a torch rack equipped
(duo_tribe_raid.py). Each with its switch and a FAIL / PASS test.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
