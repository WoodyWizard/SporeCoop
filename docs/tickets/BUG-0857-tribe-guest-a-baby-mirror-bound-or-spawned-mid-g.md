---
id: BUG-0857
title: Tribe guest: a baby mirror bound or spawned mid-growth does not follow the host's growth (age applied only at spawn) (suspected)
status: open
kind: desync
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9021
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in:
branch:
commits:
tests:
signature: 
docs: docs/re/tribe.md#tribe-fork-graduation-notes
related: BUG-0700
---

## Report
A baby member that grows up on the host may stay a baby on the guest for a while when the guest's mirror was spawned
or bound later than the host's baby was born (a guest joining or rejoining mid-growth). BUG-0700 found that a baby
the host makes while the guest is there grows up at the same second on both games.

## Root cause
* Observed (code): tribe.cpp guest_state stores the received age (tribe.cpp:2592) but `continue`s for an already bound
  mirror; age is used only for spawn_member(t, m.age) (tribe.cpp:2643). A fresh spawn starts its own growth timer.
* Observed (BUG-0700, out/lab/20261107-224702): native growth on the guest matches in the normal case.
* Suspected for the join-mid-growth case, confidence low-medium.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages PR #196, BUG-9021, switch tribe.growth); NOT FINAL: taken from the fork, verify on our code and against our rules before using.

When a snapshot says adult (age 1) for a bound native citizen that is still a baby (+0xB34 == 0), the fork calls the
citizen's native GrowUp once (modern vt+0x78, C248C0, plain ret): it runs the base growth, adds the citizen spatial
to the selectable members, updates model/scale and adult AI flags and clears the baby hit sphere, keeping the pointer
and species. Unbound local candidates are bound only if their native age equals the snapshot's. Their test spawns a
baby with SpawnMember(0), grows it on the host through the native method and checks the guest's native age, species,
pointer stability and exactly one selectable entry.

## How to test
Lab (tribe fixture): the host makes a baby; after half its growth time the guest joins (its mirror is spawned as a
baby); wait until the host's baby is adult. Assert: within 2 s the guest's mirror is adult (+0xB34 == 1) and selectable
once. FAIL on main if the suspicion holds, PASS with the GrowUp call.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id -->

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
