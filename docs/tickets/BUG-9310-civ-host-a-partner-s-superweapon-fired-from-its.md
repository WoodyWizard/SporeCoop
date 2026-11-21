---
id: BUG-9310
title: Civ (host): a partner's superweapon fired from its own nation (civ.guest_superweapons) crashes the host in FireSuperweapon's city check
status: open
kind: crash
stage: Civ
reported: 2026-11-21
source: live QA 2026-11-21 (TITAN, DuoQACivHost Abber + DuoQACivGuest), crash-20261121-035634-i9
role: host
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 read of 0x0000000C at sym 0xBF18CA (Steam runtime 0x00B618CA, base 0x370000), main thread, Civ; stack FireSuperweapon+0x176 -> 0xBDA1E0 (the city's civ check) -> CanUseSuperweapon+0x58 -> 0xBF18EB
docs:
related: BUG-9306, BUG-9309
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
Not analysed beyond the crash report (stop at the diagnosis: risky). Observed: exception at sym 0xBF18CA reading
[eax+0xC] with eax = 0, ebx = esi = 3 (the type), edi = ebp = 1C4503C0 (not one of the founder nations: seat 0
1C969DF8, seat 3 1CA35540); stack words 00000003 1C4503C0 1CA35540. The call chain is FireSuperweapon 0xBF8BB0 ->
0xBDA1D0 (per city of the firing nation: the city's civilization +0x590, then CanUse) -> CanUse 0xBF8B40 ->
the cooldown check 0xBF18D0 area. Inferred: a city in the partner nation's city list (+0x9C) has a +0x590 that is not a
complete cCivilization (its +0x490 cooldown slot null), e.g. a city the mod handed over or borrowed (team_cities /
founder nations), so the native per-city check crashes. The host's own CanUse on the partner nation (sw_refusal) passed
the same frame. The coordinator's note "type 4 at the zero vector crashed before" is likely the same crash, not the
vector. Next: open the dump, read 0xBF18xx and 0xBDA1D0, list the seat-3 nation's cities and their +0x590.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-21: opened (live QA, the host crashed; not fixed).
