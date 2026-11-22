---
id: BUG-9326
title: Tribe (host): a partner's tame order (CitizenAction 25) crashes the host: a partner village has no animal pen
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: host
versions: 0.7.14.1-lineages67-dev (fix/l47-final-tribe)
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-final-tribe, fix/l47-pens
commits: 8836d797, 212b8595, 25f2d7ba, 91de3c21
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
signature: 0xC0000005 at 0xDAD6A3 (read 0x70), main thread, in a citizen behaviour tree tick (BehaviorTreeTickGeneric+0x540); the crash text names it BT_CitizenKnockoutEnd+0x9D3 (nearest symbol, not the function)
docs: docs/re/tribe.md#tribe-final-qa, docs/re/tribe.md#tribe-partner-pens
related: BUG-9303, BUG-0856
switch: tribe.partner_pen_guard, tribe.partner_pens
---

## Report
Live QA 2026-11-21 (MSI two games, Vandh, Tribe; host instance 2, guest instance 1, fix/l47-final-tribe), testing the
partner orders of BUG-9303: the guest's two selected villagers got a tame order on a wild animal next to their village
(`coop.tribe act_obj 25 18EB45E <animal>` on the guest, the same DoActionForObjects relay a right-click uses). The host
ran it (`last order: from=1 kind=0 n=2 action=25 ... -> 325C50F0`) and crashed 19 s later: exception 0xC0000005 at
0xDAD6A3, read of 0x70, esi = the animal (crash-20261121-141748-i2). The guest stayed up and lost the host.

## Root cause
Observed (memory disassembly of the guest's running game, tools/qa_win/dis.py; the crash address is in symbol space,
the host's game ran at base 0x300000): the code at 0xDAD64D belongs to the node of CitizenAction 25 (it checks the
action record's action == 0x19). At 0xDAD676 it reads the citizen's tribe (0xC230B0: citizen +0x1014) and that tribe's
animal pen (0xC90060: `mov eax, [ecx+0x19C4]; ret`), then `lea ebx, [eax+0x70]` and `mov eax, [ebx]` without a null
check: a tribe with no pen faults there, with ebx = 0x70 as in the crash. The game makes pens for the player tribe only;
every computer village has none (docs/re/tribe.md#tribe-fork-graduation-notes, BUG-0856), and a partner founder's
village is such a village on the host. The game's own input never sends 25 for a village without a pen; the relay of
BUG-9303 (`tribe.guest_orders`) let a partner's 25 run there. Inferred, not read: the egg order (13) brings the egg to
the egg pen (+0x19C8), also null for these villages.
CORRECTION (2026-11-21, fix/l47-pens): 13's target is the egg pen itself (the game's click on it); a wild egg is order
27. The egg node reads its target as an ornament: a wild-egg target crashes it even when the village has an egg pen
(docs/re/tribe.md#tribe-partner-pens).

## Fix
Switch `tribe.partner_pen_guard` (tribe.cpp, on): the host refuses a partner's tame order (25) for a member whose
village (+0x1014) has no animal pen (+0x19C4), and an egg order (13) without an egg pen (+0x19C8), with the BUG-9303
refusal log line ("the member's village has no animal pen (BUG-9326)"). Off: the order runs (the crash). Not done: a
partner village still cannot tame by feeding or keep eggs; giving partner founder villages their own pens (the game's
player-tribe pen setup, not analysed) would make it playable (open, docs/known-issues.md#known-tribe-final-qa).

Owner decision 8A A (2026-11-21, fix/l47-pens): each partner village gets its own pens like the host's. Switch
`tribe.partner_pens` (tribe.cpp, on): the host calls the game's own pen setup `SporeCoop::cTribe_CreatePens` (0xC90080,
the call the player tribe's creation makes right before its totem) on a partner founder village right after it is
founded (before the save that carries it) and as a 5 s catch-up for a loaded world; a guest's game makes its own
village's pens the same way while its copy has none. The pens are the village's own serialized fields
(mpDomesticatedAnimalsPen, mpEggPen, mpDomesticatedAnimalsHerd), not mod objects. `coop.tribe pens` shows them. The guard
stays for any village without pens, and a partner's egg order (13) now runs only on the member's own egg pen (the game's
click on an egg pen is 13 with the pen as target; any other target is the null read, live even with the pen present).
Off (`coop.fix tribe.partner_pens off`): no partner pens, tame and egg orders refused (the guard).

## Verification
Live, MSI two games (host instance 2, guest instance 1, a fresh Creature -> Tribe entry from Vandh each time; the
guest's two villagers selected, the order from the guest with `coop.tribe act_obj`), no lab run (a Windows-only path):
- FAIL (tame, the build before the guard, 14:17:29): the host ran the partner's 25 and crashed at 14:17:48
  (crash-20261121-141748-i2, 0xDAD6A3, read of 0x70).
- PASS (tame, guard on, 14:29:03): host `tribe: refused MSI's command kind=0 action=25 ... the member's village has no
  animal pen (BUG-9326)`; the host played on for 30 more minutes. The host's own village has both pens (`mem.read
  <tribe+0x19C4> 8`: two pointers), the partner village none (8 zero bytes).
- PASS (egg, guard on, 14:31:08): `... action=13 ... no egg pen (BUG-9326)`.
- FAIL (egg, `coop.fix tribe.partner_pen_guard off` on the host, 14:58:58): the host crashed within a second
  (crash-20261121-145859-i2, 0xDB174B, read of 0: the egg node 0xDB1720 takes an object from 0xD99830(..., 0x175CDC9)
  and reads it without a null check). So the egg part is needed too, as inferred.
- Feed (20, guard does not cover it, 14:57:09): no crash; the villager carried food to the animal (item 12) and the
  animal came 40 m toward the partner village, then stopped (no pen to lead it into).

fix/l47-pens, live, MSI two games (host instance 2, guest instance 1, Creature -> Tribe from Vandh; the host's Game0
restored after each session), no lab run (a Windows-only path):
- PASS (switch on, 17:15): `tribe: seat 1's village 0000228C got an animal pen ..., an egg pen ... and a pen herd ...
  (founding)`; after the entry's share reload both games list the partner pens at (290.9 -413.5 -51.9) and (286.3 -415.9
  -56.7), the guest's from the save (made=0). Screenshots bug9326_on_host_partner_pens.png, bug9326_on_guest_partner_pens.png.
- PASS (feed and tame, 17:18-17:24): the guest's two villagers fed a wild animal (`act_obj 20`); it joined the partner
  pen herd 80 s later and walked into the pen, seen on both screens (bug9326_on_host_pet_in_partner_pen.png,
  bug9326_on_guest_pet_in_partner_pen.png). A partner's tame order 25 on another animal ran on the host, no crash in the
  following minutes (before the fix it crashed 19 s after).
- PASS (save / reload, 17:30-17:34): `coop.save`, `coop.load Game0 Vandh`, `coop.share 1`: both games show the pens and
  the pet in the pen, the egg food kept (bug9326_on_host_after_reload.png, bug9326_on_guest_after_reload.png).
- PASS (loaded world, 17:40): the saved Tribe world loaded and joined: pens on both games, from the save.
- PASS (egg, 17:41-17:56): partner egg orders 13 on its own egg pen (console and a real right-click on the guest's egg
  pen, which sent 13 by itself) ran on the host with no crash (bug9326_on_host_egg_order.png). Not shown: eggs actually
  collected; the villagers went to the eat node, the same as the host's own villagers with the same order
  (docs/re/tribe.md#tribe-partner-pens).
- FAIL as expected (guard off, pens present, 18:00): a partner's 13 on a wild egg crashed the host at once
  (crash-20261121-180002-i2, 0xDB174B, read of 0): the egg target check is needed. With the guard on (18:10): `refused ...
  an egg order's target is not the member's own egg pen (BUG-9326)`.
- FAIL (switch off on both, fresh entry, 18:06): no partner pens on either game (bug9326_off_host_no_partner_pens.png,
  bug9326_off_guest_no_partner_pens.png); `refused ... action=25 ...: the member's village has no animal pen (BUG-9326)`,
  an egg order on a pen refused.
- PASS (switch turned on in that game, 18:09): within 15 s `(catch-up)` on the host and `(this game's own village)` on the
  guest, pens at the same places (bug9326_catchup_host_pens.png, bug9326_catchup_guest_pens.png).

## History
- 2026-11-21: opened, fixed on fix/l47-final-tribe (`tribe.partner_pen_guard`), FAIL / PASS live (Verification).
- 2026-11-21: fix/l47-pens: partner founder villages get the game's own pens (`tribe.partner_pens`, owner decision 8A A);
  the egg order only on the own egg pen; live FAIL / PASS (Verification).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
