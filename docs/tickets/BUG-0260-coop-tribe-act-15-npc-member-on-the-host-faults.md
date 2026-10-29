---
id: BUG-0260
title: coop.tribe act 15 <npc member> on the host faults in cCreatureCitizen::DoAction (debug command)
status: open
kind: tooling
stage: Tribe
reported: 2026-10-29
source: dev/campaign manual probe, lane 7
role: 
versions: main 8f1944b
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
While probing the Tribe stage end for the co-op campaign (lane 7, two games, a live Creature ->
Tribe change, 15 player members after `coop.tribe spawn 12`, one NPC tribe of 3): on the host
`coop.tribe select <i> 1` for members 1..15, then `coop.tribe act 15 16` (CitizenAction 15 = attack,
target = member #16 of the NPC tribe). The command answered "action ordered (8 selected, ret 0)" and
the members walked to the target, but the host's game logged a caught fault:

    exception 0xC0000005 at 0x00AC9C7D SporeCoop::NounCast+0x1C6D (data 0x0000039C)
    called from: Simulator::cCreatureCitizen::DoAction+0x78C < ? < Simulator::cCreatureBase::GetEffectFromPools+0x300
      < Simulator::cCreatureCitizen::SetSelected+0x73 < SporeCoop::cCreatureAnimal_IsEpic+0x85E < ...

The members then stood at the target without fighting. Only the debug command is involved (players
give orders by clicks, play_tribe's raids caught no fault in the campaign runs), so this is a
test-tool problem: a test that uses `coop.tribe act` can fault the host.

How to reproduce: as above, from the `tribe` fixture after an NPC tribe appeared.

## Root cause
Not analysed. Inferred: `act` passes `local_of(id)` (a citizen of another tribe looked up among the
host's members by the coop.tribe index) and selected members' `+0xC0` sub-objects to
DoActionForObjects; the target or the object vector may not be what DoAction expects for action 15.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened by dev/campaign (seen once while probing; not used by the campaign).
