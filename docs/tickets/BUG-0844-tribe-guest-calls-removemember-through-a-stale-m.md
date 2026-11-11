---
id: BUG-0844
title: Tribe guest calls RemoveMember through a stale mirror citizen (object_alive passes on freed memory)
status: open
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9051
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: fork: coop RVA 1889AE in tribe.cpp remove_member, called by guest_tick and tick; owner field garbage
docs:
related: BUG-0426, BUG-0685
---

## Report
A Tribe guest can crash when it removes a mirror of a host member whose citizen the game has freed. The fork's
guest crashed when the host briefly came back to Tribe from the vehicle editor during graduation (Tribe -> Civ),
in remove_member: the cached citizen passed object_alive but its owner tribe pointer was garbage.

## Root cause
* Observed (code): tribe.cpp:2118 remove_member reads the citizen's owner (kOwnerTribeOff) and calls the owner's
  RemoveMember (vt 0x9C) with no check that the citizen is still a member of a live tribe.
* Observed: the snapshot end removes members the host no longer has with `if (it->second.c &&
  game::object_alive(it->second.c)) remove_member(...)` (tribe.cpp:2680); guest_state keeps `g.c` across frames and only
  checks object_alive (tribe.cpp:2614); members() (tribe.cpp:77) also filters with object_alive.
* Inferred: confidence medium-high (same pattern as the fork's crash; the trigger, native member removal while the
  host is in an editor, exists in our Tribe -> Civ). BUG-0426 lists these sites.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9051, switch tribe.mirror_member_lifetime); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

* Before any use of a cached mirror citizen, the fork validates it against the native current membership: the
  pointer must be in the member vector (+0x340 begin/end) of one of the local tribes it maps to (g_local_tribe_of), and
  RemoveMember is called only on that tribe, never through the citizen's own owner field. A citizen not found in any
  live member vector is just forgotten (g_gid_of / g_gm entry cleared), not touched.
* Switch: `tribe.mirror_member_lifetime` (COOP_FIX, BUG-9051).
* Their test (tribal-graduation-lifetime): while the host stays in the vehicle editor for 90 s, a guest-local member
  is removed through native RemoveMember; the test checks it is gone from the live member vector, then completes the
  design and transition; both games reach Civ with zero faults. The original crash report is the negative evidence.

## How to test
Lab test on the tribe fixture: during the host's City Hall or vehicle editor (or a host editor visit), remove a
guest-local mirror member natively on the guest (a test command calling RemoveMember on its tribe) and let the
guest's snapshot end run for 90 s; then the host returns and the stage continues. Assert: no fault on the guest, the
member count matches the host. FAIL on main, PASS with the fix (switch off = FAIL).

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
