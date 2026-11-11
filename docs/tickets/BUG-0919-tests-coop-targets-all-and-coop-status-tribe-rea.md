---
id: BUG-0919
title: tests: coop.targets all and coop.status (Tribe) read mod tables of the last tick without checking the game's object list
status: open
kind: test
stage: Creature,Tribe
reported: 2026-11-11
source: code audit for BUG-0917
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/known-issues.md#issue-test-commands-stale-tables
related: BUG-0917
---

## Report
Not seen at runtime. A code audit of the console commands the play bots poll (after BUG-0917, where one of them
called the game on a freed creature) found two more with the pattern "a pointer from a table of the last tick, used
without the game's object list":
* `coop.targets all`: npc::sync_entries (mods/coop/npc.cpp) and the tribe's entries (mods/coop/tribe.cpp) read the
  position and orientation of creatures from `g_host_npcs` (filtered by `g_seen_now`), the proxies and the tribe
  tables after `game::object_alive(c)` only (a read of the pointer's own +0x21). Reads only, no calls.
* `coop.status` in Tribe (tribe.cpp status): `game::scale(c)` (a virtual call) on the chiefs of `g_chief` without
  `active()`; coop_long_session.py asks `coop.status` right after stage changes.

## Root cause
By code only (not reproduced): commands run between frames; `g_seen_now` is not rebuilt while the simulation stands
(pause, the death caption, a cinematic) and the guest's proxies are checked against the object list once a second.
Safe by the audit: `coop.targets` (plain), `coop.npc`, `coop.team`, `coop.avatar`, `coop.status` in Creature,
`coop.social`, `coop.dna`, `coop.cam`, `coop.abilities` (list, play). Not audited: `coop.uistate`,
civ::for_each_target, spacecoop::nav_entries. `coop.npc list` was fixed with BUG-0917.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not reproduced. Next: one pass over `game::for_each_object` at the start of sync_entries and of the tribe status
(as `listed_now` in npc.cpp's list), then use only pointers found there.

## History
- 2026-11-11: opened.
