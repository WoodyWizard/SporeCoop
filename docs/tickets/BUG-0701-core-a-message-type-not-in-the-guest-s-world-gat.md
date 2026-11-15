---
id: BUG-0701
title: core: a message type not in the guest's world gate was applied from the host while the guest played another world (hand list, new types open by default)
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: K2a audit of BUG-0420 (0.7.11)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2a-state
commits: 6b49ee3d 9792469d
tests: validate_tests (make test: host_world msg_class); world_gate_vote
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0420, BUG-0231
---

## Report
K2a audit of the class BUG-0420 (item 6 of its list, BUG-0201): the message world gate of a guest elsewhere was a hand list (coop.cpp world_state_msg); a message type not on it, including every new one, was applied from the host while the guest played another world.

## Root cause
Observed in the code: `world_state_msg` listed 19 types; MSG_VOTE and every module-gated type (NPC, WOBJ, Space, effects) relied on their modules, and nothing checked that a new type was considered at all.

## Fix
`host_world.h msg_class()`: one class per MsgType (Session, HostWorld, Module, Unknown) in a switch without default under `#pragma GCC diagnostic error "-Wswitch"`: an unclassified MsgType fails the build of coop.dll and of tests/validate_tests.cpp (checked by removing one case: "enumeration value 'MSG_CHAT' not handled in switch [-Werror=switch]"). The dispatch gate drops HostWorld and, with `core.msg_classes` (default on), Unknown types while elsewhere (default deny). MSG_VOTE is Session with its own gate (BUG-0702).

## Verification
no-fail-run: a build-time rule; tests/validate_tests.cpp checks the classes, that the 0.7.10 hand list is a subset of HostWorld (no message newly opened) and the default deny of unknown types (make test). The world gate's lab tests pass: out/lab/20261107-222542 (world_gate_vote).

## History
- 2026-11-07: opened.
- 2026-11-07 (fix/w46-k2a-state): fixed, core.msg_classes.
- 2026-11-08: released in 0.7.11.
