---
id: BUG-9047
title: A founder village can consume a native NPC slot and prevent the fifth Tribal victory
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Native campaign completion probe with two surviving founders
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_objective_budget.py
switch: tribe.founder_npc_budget
signature: four earned segments, no eligible NPC tribe, both founders survive
docs: docs/re/tribe.md
related: BUG-9046 BUG-9040 BUG-9002
---

FAIL m2-tribal-finish-b.log: the Clyth team earns its fourth native segment
after destroying21F7. Its only intact non-founder village21E2 is already allied;
21E0/21E1/21F7 are defeated, and both founder villages survive. There is no
eligible fifth NPC. Existing native-controller RE says FormVillage CC6440
counts all tribes against its property list and uses that count as its species
index. Verify the count path and remaining spawn markers before changing it.
The fixture predates automatic founding, so repeat the resulting fix on a
fresh auto-founded campaign before claiming general completion.

The fix should retain five normal NPC objectives and exclude the cooperative
founder from that NPC budget. Do not grant a free victory for having a partner
or require either founding species to be conquered.

Cached decoded CC6440 calls GetData B212D0 at+38, consumes only its vector
size at+3D/+40 and shifts by2. It compares twice that count with its tuning
array and uses count-1 for NPC species selection. A caller-scoped private
count view excludes the registered guest without altering the native vector.
GetData signature verified at ret14+C7; callback arguments remain unchanged.

Fixed-a cold test still FAILED: native formation calls=0 for45 seconds. That
saved campaign had already consumed its native formation trigger. The explicit
native-operation probe m2-tribal-budget-evaluate-a.log reproduces no new village
with the fix off, then creates native NPC2228/species067B3AFA with it on, total7,
zero native faults. This manual call is mechanism evidence, not automatic
qualification. CORRECTION: FormVillage EAX is not a Boolean success flag
(off12/on28); inspect actual native villages instead. Its sole stack argument
is not read in the decoded body; the verified probe supplies0.

Backfill now retries the same native operation outside-frame only for the
specific stranded case: four earned segments, six total villages, both founders
alive and every existing NPC defeated or allied. It grants no history/reward.
The native controller still chooses the marker, species and normal village setup.

Automatic recovery m2-tribal-budget-fixed-b-ready.log passed on both isolated
games: seven native tribes and one eligible NPC, still exactly four victories.
The subsequent m2-tribal-finish-c.log ordered guest citizens against that NPC.
Native AI delivered the lethal strike; both games reached five native totem
segments with the two founding villages intact and zero faults. Native History
then offered Civilization graduation. Fresh automatic-founding coverage remains.

Fresh-campaign qualification (2026-11-05): BUG-9059 corrects the earlier
appearance gate as well as this factory budget. Automatically founded
Vantiegan earned all five NPC objectives in tribal-final-campaign-g.log and
tribal-final-campaign-h.log. Native controller appearance and native ordered
AI kills, no injected formation or victory. The separate final native readback
tribal-final-five-readback.log confirms both founders retain their huts and
populations; five earned totem types/colors and unlocks agree on both games.
Original species remain291489FF/7CF26CCB, with zero recorded faults.
