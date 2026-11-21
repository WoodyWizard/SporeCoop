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
branch: fix/l47-powers
commits: 0306419f
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

2026-11-20 (fix/l47-powers), observed in the code and in Ghidra (GOG 2024, docs/re/tribe.md#tribe-chief-powers):
* The game's own input (DoActionGeneric 0xCD0C20) makes these orders: 0x21 food raid -> 8 on the OTHER village's cTribe
  (not the mat); 0x22 fish -> 1 with no target and a Vector3 property (the fishing spot); 0x1B bundle -> 14; 0x1C/0x1D
  meat -> 27; 0x23 egg -> 13; 0x24 loose fruit -> 2 on the cFruit; 0x29 -> 20 feed / 25 tame on the animal. The guest's
  relay (h_DoActionForObjects) sent the target but dropped the property, so a guest's fishing spot could not reach the
  host even had the order been allowed.
* chiefs_tick's table g_chief holds one chief per owner from the host's player tribe only; a partner founder's members
  live in their own village, so near_own_chief found no chief for them (tribe.chief_aura never applied). The chief rescue
  (vt == pt) is also host-village only; not changed here.
* CORRECTION (2026-11-20, fix/l47-powers): "Firebombs" in the owner's words is the chief's Fire Bomb stage power, not
  torches (owner: "They don't need to hold torches, the chieftain will always be able to throw the bombs"); that part is
  BUG-9306. The torch-rack question above stays open but is not this report.

## Fix
Done on fix/l47-powers (built; not run in a game yet):
* `tribe.guest_orders` (BUG-9303): host_command accepts a partner village commander's fishing (1, the spot in the
  command's position, set on the action record's +0x39 as a walk's), meat / egg / bundle (27 / 13 / 14: the target an
  object of the host's world, not a member, not a party member's), food raid (8: another living, unconquered,
  non-founder village), repair (24: a tool or hut of the sender's own village), feed / tame (20 / 25: a living animal)
  and loose-fruit gathers (2 on a cFruit, then the tribe.fruit_order fallback); the members must be adults and up. The
  guest's relay reads a fishing order's Vector3 property. Every refused command is logged on the host (one line a
  second, the rest counted into the next line: `tribe: refused <name>'s command kind= action= ... : <reason> (+N
  refusals not logged; M in all, BUG-9303)`), the reason named where a check sets one (the allowlist, the new checks,
  the chief power), else "a target or member check". Off: as before (dropped, counted only, fishing without a spot).
* `tribe.guest_chief` (BUG-9303): chiefs_tick also records each partner founder village's living native chief (tribe
  +0x11C) for its seat's player; near_own_chief uses it, so the chief aura (+20 % damage within 15 m of the own chief)
  works for partners. No size marker (the game's chief scale stays). Off: no aura for partners.
* Not done: the chief rescue for a partner chief; the torch-rack repro; a reason on each of the ~40 old refusal places
  (they log as "a target or member check" with the command's kind, action and target).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): partial: the refusal log works ("tribe: refused MSI's command kind=16 ...: the power is recharging"). Fishing, meat, eggs, bundles, raids, repair, taming and loose fruit not tested (they need right-clicks on targets).

- Live QA 2026-11-21, second run (QA agent, fix/l47-qa 9d64f85d = fix/l47-powers aeaedfc3 + BUG-9308, MSI two games: host MSITribeHost instance 2, guest MSIGuest instance 1, a live Creature -> Tribe entry from Vandh): partial. A real guest right-click on a loose fruit (noun 0x2C9CC91) with two
  villagers selected (box drag): the order went to the host (guest cmds_sent 1 -> 2, host cmds_done 3, rejected 0, no
  "tribe: refused" line all session), the selected villagers walked to the fruit on the guest's screen
  (out/qa-shots/t_g_fruit.png); the fruit was still listed 12 s later (gather result not confirmed). Fishing, carcass
  meat, eggs, bundles, food raid, repair, tame / feed: not reached (no targets near the villages in this world; `coop.tribe
  act` only takes members / proxies).

## History
- 2026-11-20: opened.
- 2026-11-20: fix/l47-powers: tribe.guest_orders, tribe.guest_chief (static analysis + build only; the two-player test is for the lead).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: second live QA run (see Verification).
