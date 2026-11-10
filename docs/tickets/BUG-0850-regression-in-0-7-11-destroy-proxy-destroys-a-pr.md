---
id: BUG-0850
title: Regression in 0.7.11: destroy_proxy destroys a proxy the game already removed (broken if/else after BUG-0710)
status: open
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan), found while checking fork PR #196 BUG-9012
role: guest
versions: 0.7.11, 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature: 
docs:
related: BUG-0710, BUG-0426
---

## Report
A Creature guest that drops a host animal's proxy which its own game had already destroyed calls the game's
DestroyInstance on it a second time. Inferred: a possible crash or heap corruption on the guest when host animals go
stale (host editor, load, despawn). Hotfix candidate.

## Root cause
* Observed (code): npc.cpp:1673 destroy_proxy:
  `if (game::is_destroyed(c)) game::puppet_unregister(c); else creature_hp::forget_puppet(c); game::destroy_creature(c);`
  without braces. Commit 97392cd1 (BUG-0710, first in v0.7.11) inserted the forget_puppet line into the unbraced
  else, so destroy_creature now runs in both branches; the comment says the destroyed case should "only forget it".
* Inferred: confidence high for the logic error, medium that the double DestroyInstance crashes (the game may ignore
  an already destroyed instance). -Wall should report it as misleading indentation (check the build log).

## Fix idea
Put braces around the else branch (forget_puppet + destroy_creature) so a proxy the game destroyed is only
unregistered. Check the other BUG-0710 insertions of 97392cd1 (guest_tick g_kill loop, remove_saved_strays): they are
braced or single-statement and fine. Consider -Werror=misleading-indentation for coop.dll.

## How to test
Lab test: creature fixture, guest; let the guest's own game destroy a proxy (a test command that destroys the proxy's
creature natively), then make the host stop sending it (stale drop, or host editor) so destroy_proxy runs. Assert via a
counter (or a hook on DestroyInstance) that the destroyed proxy is not destroyed again, and no fault. FAIL on 0.7.11,
PASS with braces.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
