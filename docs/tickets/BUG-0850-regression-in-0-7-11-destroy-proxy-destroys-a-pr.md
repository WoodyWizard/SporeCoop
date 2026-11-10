---
id: BUG-0850
title: Regression in 0.7.11: destroy_proxy destroys a proxy the game already removed (broken if/else after BUG-0710)
status: fixed
kind: crash
stage: Creature
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan), found while checking fork PR #196 BUG-9012
role: guest
versions: 0.7.11, 0.7.11.1
fixed_in: unreleased
branch: fix/w47-proxy-destroy
commits: aa725a1a
tests: npc_destroy_once, npc_destroy_once_tribe
signature: second cGameNounManager::DestroyInstance of a destroyed proxy (destroy_proxy, npc.cpp)
docs: docs/06-testing.md#testing-destroy-count
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
Braces in destroy_proxy: a proxy the game destroyed is only unregistered; the live one is forgotten by creature_hp and
destroyed (as before 97392cd1). `coop.fix npc.destroy_once off` (COOP_FIX_UNSAFE: its off side destroys an object
twice) restores the 0.7.11.1 path. The other BUG-0710 insertions of 97392cd1 (guest_tick g_kill loop,
remove_saved_strays) are in braced blocks and fine. The Makefile's COMMON flags have -Werror=misleading-indentation
(llvm-mingw -Wall does not include it); with it on, this line was the only hit in coop.dll, sporekit.dll and the loader,
and none in tests/*.cpp. Test command `coop.npc gamekill <id>` (guest): the game's own DestroyInstance of the proxy,
then destroy_proxy in the same call; gamefixes' DestroyInstance hook counts the calls of that object.

## Verification
coop_npc_destroy_once.py: 3 living proxies near the guest, `coop.npc gamekill` each, count DestroyInstance.
* FAIL (SPORE_FIX_OFF=npc.destroy_once): 20261110-173401 (creature: {2: 2, 1: 2, 5: 2} calls), 20261110-173820
  (tribe: {6: 2, 13: 2, 28: 2}). No fault in either run: the game tolerated the second destroy within the same frame,
  so the crash itself stays inferred (the object is freed later; a destroy of it after that would read freed memory).
* PASS (fix on): 20261110-173543 (npc_destroy_once and npc_destroy_once_tribe, 2x each, 1 call per object, no fault).

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-10: fixed on fix/w47-proxy-destroy (hotfix for 0.7.11.2): braces, switch npc.destroy_once, test npc_destroy_once.
