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

## Root cause
Not analysed.

## Fix
None.

## Verification
no-fail-run: not reproduced.

## History
- 2026-11-01: opened.
