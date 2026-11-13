---
id: BUG-1043
title: Tribe: a host member that puts its tool down (tool 0) keeps the tool on the guest's mirror: the field relay drops values under 0.001 (also hunger 0)
status: open
kind: desync
stage: Tribe
reported: 2026-11-13
source: audit 2026-11-12 (0.7.14) SPC-8
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-tribe-tool-zero
related: BUG-0514, BUG-0935, BUG-0987
---

## Report
Audit SPC-8 (first half). Not seen at runtime, found by code reading.
A host tribe member that puts its tool down (its tool type becomes 0) keeps the tool on the guest's mirror: the guest's mirror still has the weapon / instrument tool type (its abilities, possibly its role icon). The same drop hits a member's hunger of 0 (a starving member): the guest's mirror keeps its last hunger above 0.

## Root cause
Observed in code (main efd2e0ba):
* mods/coop/fields.cpp:252 the guest drops every received value outside `fields_wire::sane(v, 0.001f, 1000.f)`, for all kinds and fields; the host sends the plain value (fields.cpp:121-150).
* tribe.cpp:880 `read_held_tool` sends mSpecializedTool (+0x102C) as is: 0 = no tool (docs/re/tribe.md: SetSpecializedTool, "0 on the leader becomes 0xB"; `apply_held_tool` at tribe.cpp:881-896 handles t == 0 explicitly, so 0 was meant to arrive).
* tribe.cpp:1175 `read_hunger` sends +0xBBC (0..100): 0 is dropped too.
* The guest's want for that member keeps the old value; after kForget (12 s, fields.cpp:31, 178-181) it is forgotten without applying anything, so the mirror keeps the old tool / hunger. The held item field avoids this by sending item + 1 (tribe.cpp:921-924 comment: "values under 0.001 are not sent").
Inferred: how often the host's members go back to tool 0 in play (a tool returned to a rack, a destroyed rack, a role change); the thought bubble difference of BUG-0987 may be related (not checked). Confidence: high for the drop, medium for what the player sees.

## Fix idea
A per-field sane range (Field gets lo / hi; scale keeps 0.001..1000, tool 0..0x20, hunger 0..100), or encode the tool as type + 1 like the held item. Switch: `coop.fix fields.zero_values off` (the old global range).

## How to test
Extend `coop_tribe_tools.py` (or a new `coop_tribe_tool_drop.py`): after the member holds the weapon on both games, the host sets its tool back to 0 (`coop.tribe` test command calling SetSpecializedTool(0) on a non-leader, or the game's return-to-rack order); within 5 s the guest's mirror +0x102C must be 0 (the game's field, `coop.tribe` row). Main: stays 1 (FAIL). Hunger: `coop_tribe_mirror_hunger.py` with a member at hunger 0.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
