---
id: BUG-1043
title: Tribe: a host member that puts its tool down (tool 0) keeps the tool on the guest's mirror: the field relay drops values under 0.001 (also hunger 0)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-8
role: 
versions: 
fixed_in: 0.7.14
branch: fix/w49-small
commits: f4aca716
tests: tribe_zero_fields
signature:
docs: docs/known-issues.md#issue-tribe-tool-zero, docs/06-testing.md#testing-t1-small
related: BUG-0514, BUG-0935, BUG-0987
---

## Report
Audit SPC-8 (first half). Not seen at runtime, found by code reading.
A host tribe member that puts its tool down (its tool type becomes 0) keeps the tool on the guest's mirror: the guest's mirror still has the weapon / instrument tool type (its abilities, possibly its role icon). The same drop hits a member's hunger of 0 (a starving member): the guest's mirror keeps its last hunger above 0.

## Root cause
Confirmed at runtime: fields.cpp on_message dropped every value outside 0.001 .. 1000 for every field; the host's
member with tool 0 kept tool 1 on the guest's mirror, and a host member at hunger 0 kept 93.5 there (FAIL run).

## Fix idea
A per-field sane range (Field gets lo / hi; scale keeps 0.001..1000, tool 0..0x20, hunger 0..100), or encode the tool as type + 1 like the held item. Switch: `coop.fix fields.zero_values off` (the old global range).

## How to test
Extend `coop_tribe_tools.py` (or a new `coop_tribe_tool_drop.py`): after the member holds the weapon on both games, the host sets its tool back to 0 (`coop.tribe` test command calling SetSpecializedTool(0) on a non-leader, or the game's return-to-rack order); within 5 s the guest's mirror +0x102C must be 0 (the game's field, `coop.tribe` row). Main: stays 1 (FAIL). Hunger: `coop_tribe_mirror_hunger.py` with a member at hunger 0.

## Fix
fields::Field has its own accepted range (lo / hi, default 0.001 .. 1000); the tribe tool (0 .. 32) and hunger
(0 .. 100) fields accept 0. Switch `coop.fix fields.zero_values off`: the one range for every field (0.7.13).

## Verification
Test `tribe_zero_fields` (coop_tribe_zero_fields.py: `coop.tribe settool <id> 1` then `0`, `coop.tribe hunger <id> 0`
on the host; the guest's row of that member follows within 5 s).
FAIL (SPORE_FIX_OFF=fields.zero_values): out/lab/20261113-031231 (guest tool 1, hunger 93.49). PASS: out/lab/20261113-030613,
out/lab/20261113-031904.

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-small (package T1).
- 2026-11-14: released in 0.7.14.
