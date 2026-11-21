---
id: BUG-9326
title: Tribe (host): a partner's tame order (CitizenAction 25) crashes the host: a partner village has no animal pen
status: open
kind: crash
stage: Tribe
reported: 2026-11-21
source: QA 2026-11-21
role: host
versions: 0.7.14.1-lineages67-dev (fix/l47-final-tribe)
fixed_in:
branch: fix/l47-final-tribe
commits:
tests:
signature: 0xC0000005 at 0xDAD6A3 (read 0x70), main thread, in a citizen behaviour tree tick (BehaviorTreeTickGeneric+0x540); the crash text names it BT_CitizenKnockoutEnd+0x9D3 (nearest symbol, not the function)
docs: docs/re/tribe.md#tribe-final-qa
related: BUG-9303, BUG-0856
switch: tribe.partner_pen_guard
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

## Fix
Switch `tribe.partner_pen_guard` (tribe.cpp, on): the host refuses a partner's tame order (25) for a member whose
village (+0x1014) has no animal pen (+0x19C4), and an egg order (13) without an egg pen (+0x19C8), with the BUG-9303
refusal log line ("the member's village has no animal pen (BUG-9326)"). Off: the order runs (the crash). Not done: a
partner village still cannot tame by feeding or keep eggs; giving partner founder villages their own pens (the game's
player-tribe pen setup, not analysed) would make it playable (open, docs/known-issues.md#known-tribe-final-qa).

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

## History
- 2026-11-21: opened, fixed on fix/l47-final-tribe (`tribe.partner_pen_guard`), FAIL / PASS live (Verification).
