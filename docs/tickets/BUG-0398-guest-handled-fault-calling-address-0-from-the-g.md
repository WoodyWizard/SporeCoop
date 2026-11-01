---
id: BUG-0398
title: Guest: handled fault calling address 0 from the game's input update (GameInput::IsMouseButtonDown+0x86 < cAppSystem::Update) during npc_walk_player
status: open
kind: crash
stage: Creature
reported: 2026-11-01
source: found on fix/w30-npc-walk (lab 20261101-121954 npc_walk_player#1)
role: guest
versions: 0.7.3.3
fixed_in:
branch: fix/w30-npc-walk
commits:
tests: npc_walk_player
signature: 0xC0000005 at 0x00000000, calls 008D33C6 007EA30F 00F47947 00F47A5D 00EC6798 (GOG 2024)
docs:
related:
---

## Report
out/lab/20261101-121954-fix-w30-npc-walk npc_walk_player#1 (guest at 30 fps over SPORE_NET=player, the test's
camera right-drags on the guest): the guest's game logged a handled exception 0xC0000005 at 0x00000000 (a call
to a null pointer), `calls: 008D33C6 007EA30F 00F47947 00F47A5D 00EC6798 0088568C 00F47234 0084BD01`, called from
`GameInput::IsMouseButtonDown+0x86 < App::cAppSystem::Update+0x48F < App::cSporeApp::Shutdown+0x3A7` (nearest
symbols, likely not exact). The game went on (the test's other checks passed apart from one proxy jump). coop.dll
is not in the stack. 41 s after the start, 2 s after the guest's frame of 5.4 s (loading). Seen once in about 30
walk-test runs that day.
Again in out/lab/20261101-125335-fix-w30-npc-walk puppet_slope (guest at 30 fps over SPORE_NET=player, after
`coop.abilities goto`): 0xC0000005 at 0x00000000, `calls: 0070693E 004234E8 0041DDF4 00537FF0 004232DB 0041DD60`
(nearest symbols HierGrid_Remove+0x3B0E < App::Property::SetValueKey+0x5A8), then the guest froze (watchdog
"no frames" 30 s later) and wrote a report. Calls to address 0 also in main's lab runs 20261030-104149 and
20261031-054758 (other stacks): a family of null calls in the game, not tied to coop.dll by the stacks.

## Root cause
Not analysed.

## Fix
None.

## Verification
no-fail-run: not reproduced.

## History
- 2026-11-01: opened.
