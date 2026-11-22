---
id: BUG-9310
title: Civ (host): a partner's superweapon fired from its own nation (civ.guest_superweapons) crashes the host in FireSuperweapon's city check
status: fixed
kind: crash
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, DuoQACivHost Abber + DuoQACivGuest), crash-20261121-035634-i9
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-civ
commits: ffe972b0
tests: manual two-game live test (docs/06-testing.md#testing-partner-superweapon); no lab test yet
signature: 0xC0000005 read of 0x0000000C at sym 0xBF18CA (Steam runtime 0x00B618CA, base 0x370000), main thread, Civ; stack FireSuperweapon+0x176 -> 0xBDA1E0 (the city's civ check) -> CanUseSuperweapon+0x58 -> 0xBF18EB
docs: docs/re/civ.md#civ-partner-sw-tools
related: BUG-9306, BUG-9309, BUG-9311, BUG-9312
---

## Report
Live QA 2026-11-21 (QA agent, TITAN QA copy, build fix/l47-qa = fix/l47-powers aeaedfc3 + BUG-9308): host
DuoQACivHost / Abber (Civ, 4 founder seats; for the test the guest's identity was written into seat 3 of the copy's
`.duonations`, see BUG-9309), guest DuoQACivGuest = seat 3 (nation 1CA35540, faction 21CB). Host
`coop.settings force civ.superweapon_vote 0`, then (within one 0.5 s campaign tick, see below) host `coop.civ swgrant 3
3 20000` and guest `coop.civ super 3`. Host log 03:56:34.406 "civ: host: CivGuest's superweapon 3 nation=1CA35540 (host's
1C969DF8): fires (BUG-9306)", and the host crashed in the same frame: crash-20261121-035634-i9 (.txt / .dmp / report
zip in the QA copy's mods\logs).
Also seen: `coop.civ swgrant` is undone by the 0.5 s campaign tick (civ.cpp campaign_tick -> h_Unlocks ->
RefreshUnlocks for every founder nation, `civ.partner_unlocks`): seat 3's unlocked[3] was 1 for under 0.3 s, so a
request a second later is refused "unlocked=0". The recipe in BUG-9306 (swgrant, then super, then the vote) cannot pass
with aeaedfc3 in; the grant and the request had to be sent back to back with the vote off.

## Root cause
First reading (crash report only; superseded by the CORRECTION below). Observed: exception at sym 0xBF18CA reading
[eax+0xC] with eax = 0, ebx = esi = 3 (the type), edi = ebp = 1C4503C0 (not one of the founder nations: seat 0
1C969DF8, seat 3 1CA35540); stack words 00000003 1C4503C0 1CA35540. The call chain is FireSuperweapon 0xBF8BB0 ->
0xBDA1D0 (per city of the firing nation: the city's civilization +0x590, then CanUse) -> CanUse 0xBF8B40 ->
the cooldown check 0xBF18D0 area. Inferred: a city in the partner nation's city list (+0x9C) has a +0x590 that is not a
complete cCivilization (its +0x490 cooldown slot null), e.g. a city the mod handed over or borrowed (team_cities /
founder nations), so the native per-city check crashes. The host's own CanUse on the partner nation (sw_refusal) passed
the same frame. The coordinator's note "type 4 at the zero vector crashed before" is likely the same crash, not the
vector. Next: open the dump, read 0xBF18xx and 0xBDA1D0, list the seat-3 nation's cities and their +0x590.

CORRECTION (2026-11-21, fix/l47-civ): not an incomplete civilization and not a borrowed city. Found (Ghidra on the
TITAN Steam dump, GOG 2024 addresses; the crash dump; a live run):
* The fault is in 0xBF18C0 (thiscall(civ, type), ret 4: `mov eax,[ecx+0x470]; mov eax,[eax+type*4]`): the nation's
  superweapon tool vector +0x470 is empty (begin = 0), read at type 3 -> 0xC. The real chain (the stack scan's other
  return addresses are stale) is FireSuperweapon+0x176 (0xBF8D26) -> the launch 0xBE5D30(city, type, target) ->
  0xBDC2F0 (types 0, 1, 3, 4, 6, 7, 9, 10, 11; 0xBDC308, the dump's [esp], is its return) -> 0xBF18C0 with ecx = city
  +0x590. The city (edi = ebp = 1C4503C0, vtable 0x1468790, +0xC = nation + 0xC) is seat 3's own; its +0x590 is seat
  3's nation 1CA35540, a complete cCivilization (vtable 0x1469370).
* The twelve tools are loaded by 0xBF7020 (now Simulator::cCivilization::LoadSuperweaponTools) only when the nation's
  +0x89 (mIsPlayerOwned) is set: at creation (0xBF82B0) and after a load (Read, vtable slot 0xBF8280). In the dump the
  host's nation 1C969DF8 has +0x89 = 1, seat 3's nation 1CA35540 has +0x89 = 0 (+0x8A = 1). So a partner founder's
  nation never had tools and every launch from it read a null vector. The cooldown check 0xBF18D0 in CanUse reads the
  same vector while a type recharges (the same crash for a nation without tools; a partner never got that far).
* Live (fix on): the first `coop.civ sw` on the host logged `nation 1C3ADE18 (player-owned 0) got its superweapon
  tools: 12 loaded` for seats 1, 2 and 3: no partner nation had tools.
* The report's symbolization ("FinishBuyCity+0x12CA", "cTurret_vtable+0x194C") is the nearest known name, not the
  function: 0xBF18CA is inside the 16-byte 0xBF18C0.
* `coop.civ swgrant` lost its grant because the campaign tick runs h_Unlocks -> RefreshUnlocks for every founder nation
  every 0.5 s, and RefreshUnlocks first clears +0x484 (0xBF251D).

## Fix
fix/l47-civ ffe972b0, `civ.partner_sw_tools` (on): before a nation's weapon is checked (CanUse through sw_refusal, which
`coop.civ sw`, `swgrant` and the partner request use) or fired (the three o_FireSW paths of host_superweapon_request),
ensure_sw_tools fills its +0x470 with the game's own loader 0xBF7020 when the vector is short or has a null entry, with
+0x89 set to 1 for that call only (a swap ledger entry, then put back). Logged per nation: `civ: nation <p>
(player-owned 0) got its superweapon tools: 12 loaded (BUG-9310)`. `coop.fix civ.partner_sw_tools off`: no tools (the
crash). Test tool: `coop.civ swgrant <seat> <type> [money]` now keeps its grant across RefreshUnlocks (a per-seat mask
applied after each h_Unlocks; `coop.civ swgrant clear`; reset on a planet change).
Not changed: who the global types 2 / 5 / 8 hit (decision 7P); a weapon's visuals on a guest (BUG-9312).

## Verification
Live, TITAN QA copy (build fix/l47-civ ffe972b0), host DuoQACivHost / Abber (instance 9; for the runs the save's
`.duo-seats` seats byte 0x10 -> 4 and the guest's identity in seat 3 of `.duonations`, the folder restored as found
afterwards), guest DuoQACivGuest (instance 10) = seat 3 ("CivGuest takes seat 3 of Fourth"). Host `coop.settings force
civ.superweapon_vote 0`, `coop.civ swgrant 3 3 20000`; guest `coop.civ super 3 75 -410 -285` (a non-global type at open
ground beside the host's capital, in both cameras).
- FAIL (switch off: `coop.fix civ.partner_sw_tools off` on both games before the load): `coop.civ sw` tools_loaded=0;
  host 04:39:32.118 "CivGuest's superweapon 3 nation=1C850918 ...: fires", then the host crashed:
  crash-20261121-043932-i9 (0xC0000005 at runtime 0x00B618CA = 0xBF18CA, read of 0x0000000C, eax=0, ebx=esi=3, the
  firing nation 1C850918 on the stack): the report's signature.
- PASS (switch on): seat 3's grant held 3 s across the campaign tick (unlocked=000110000000); host 04:36:35.106
  "CivGuest's superweapon 3 nation=1ADA8708 ...: fires"; both games ran on, no crash report; seat 3's own money 20000 ->
  18000 (the shared money untouched); type 3 then recharging (can=0: CanUse's cooldown check read the loaded tools). The
  target marker and a green burst on the host's screen (out/qa-shots/bug9310_host_partner_sw3_target.png,
  bug9310_host_partner_sw3_impact.png); nothing drawn on the guest's screen at 1 / 3 / 6 s
  (bug9310_guest_partner_sw3_no_visual.png).
- PASS (an effect on the game state, both games): seat 3 granted type 0; guest `coop.civ super 0 167 -479 -3` (seat 1's
  city, an offline founder's, NPC-run): that city's hp 7600 -> 4610 on the host 6 s later, 4640 on the guest (the
  building hp relay); seat 3 money -4000; no crash.
- Baseline: the host's OWN type 3 (`coop.civ swgrant 0 3`, host `coop.civ super 3 ...`) is not drawn on the guest either
  (out/qa-shots/bug9310_baseline_host_own_sw3_host_left_guest_right.png): no superweapon visual reaches a guest; opened
  as BUG-9312, not part of this fix.
- Not run: a global type (2 / 5 / 8; decision 7P open), a partner type recharging across a save and load.

## History
- 2026-11-21: opened (live QA, the host crashed; not fixed).
- 2026-11-21: root cause found (a partner nation has no superweapon tools); fixed on fix/l47-civ (civ.partner_sw_tools), swgrant grants last; live FAIL / PASS on TITAN.
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
