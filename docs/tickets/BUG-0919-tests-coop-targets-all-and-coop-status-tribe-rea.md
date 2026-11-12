---
id: BUG-0919
title: tests: coop.targets all and coop.status (Tribe) read mod tables of the last tick without checking the game's object list
status: fixed
kind: test
stage: Creature,Tribe
reported: 2026-11-11
source: code audit for BUG-0917
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-rest
commits: 339c3dc4
tests: status_stale_chief, sync_tribe, play_tribe
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
Reproduced (2026-11-12, fix/w48-rest) for `coop.status` in Tribe: `coop.tribe drop_chief <owner>` and `coop.status` sent
3 ms apart on two console connections run in the same pump of the main thread (no tick between): the status still had
the dropped chief in `chiefs=` and printed its size from a GetScale call on the destroyed citizen (`chief_scale= 0:1.30`)
in 2 of 2 rounds. Was (by code): commands run between frames; `g_seen_now` is not rebuilt while the simulation stands
(pause, the death caption, a cinematic) and the guest's proxies are checked against the object list once a second.
Safe by the audit: `coop.targets` (plain), `coop.npc`, `coop.team`, `coop.avatar`, `coop.status` in Creature,
`coop.social`, `coop.dna`, `coop.cam`, `coop.abilities` (list, play). Not audited: `coop.uistate`,
civ::for_each_target, spacecoop::nav_entries. `coop.npc list` was fixed with BUG-0917.

## Fix
npc.cpp / tribe.cpp `sync_entries` (coop.targets all, the sync-check) and tribe.cpp's status: a creature or chief from
the tables of the last tick is used only when the game's object list has it now (`game::listed`, inside a
`game::LiveScope`: one walk of the list per command); the status prints `<owner>:gone` for such a chief. Switch:
`coop.fix npc.entries_listed off` (all three). civ::for_each_target, spacecoop::nav_entries and coop.uistate were not
changed (not audited here).

## Verification
Test `status_stale_chief` (tools/tests/coop_status_stale_chief.py, tribe fixture): the drop and the status in the same
pump, twice (the host's and the guest's chief); the status must say `gone` for the dropped chief.
FAIL (the code before the fix, a temporary worktree, the run copied here): out/lab/20261112-144258 (`0:1.30`, `1:1.30`).
PASS: out/lab/20261112-144419, out/lab/20261112-144421 (`0:gone`, `1:gone`).

## History
- 2026-11-11: opened.
- 2026-11-12: reproduced for the Tribe status (same-pump drop), the entries and the status ask the object list (npc.entries_listed), fix/w48-rest.
