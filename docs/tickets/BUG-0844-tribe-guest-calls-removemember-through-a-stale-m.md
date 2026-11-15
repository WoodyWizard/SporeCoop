---
id: BUG-0844
title: Tribe guest calls RemoveMember through a stale mirror citizen (object_alive passes on freed memory)
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9051
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: 0.7.13
branch: fix/w48-freed
commits: 570ee491
tests: tribe_mirror_lifetime
signature: fork: coop RVA 1889AE in tribe.cpp remove_member, called by guest_tick and tick; owner field garbage
docs: docs/re/tribe.md#tribe-mirror-citizen-reuse, docs/06-testing.md#testing-freed-block-reuse
related: BUG-0426, BUG-0685, BUG-0955
---

## Report
A Tribe guest can crash when it removes a mirror of a host member whose citizen the game has freed. The fork's
guest crashed when the host briefly came back to Tribe from the vehicle editor during graduation (Tribe -> Civ),
in remove_member: the cached citizen passed object_alive but its owner tribe pointer was garbage.

## Root cause
Observed at runtime (fix/w48-freed, 2026-11-12): a citizen the game removed (RemoveMember with destroy) is freed later and its block goes to the next citizen the game makes, of any tribe (`coop.tribe reusehunt`: the guest's player tribe got a removed NPC-tribe mirror's block). While the host's states paused (`coop.tribe statehold`, as during the host's editor at graduation: the fork's report) the mirror stayed bound to the address; object_alive (+0x21 of that block) passed for the new citizen, so when the states came again the guest drove the guest's own new member as the host's member of the other tribe (lab 20261112-010419, mirror #7). remove_member read the owner tribe from the citizen's memory (the fork's crash on a freed block). Confidence: high.

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
mods/coop/tribe.cpp: a mirror records its citizen's serial when it is bound; `mirror_ok` (game::alive: listed as a citizen with that serial) replaces object_alive in guest_state, guest_tick and the snapshot's end; remove_member calls RemoveMember only on a live tribe that has the citizen in its member vector (`tribe_holding`), else the citizen is only forgotten. Switch: `coop.fix tribe.mirror_member_lifetime off`. Test hook: `coop.tribe reusehunt any|npc|<id> [frames]`.

## Verification
Test `tribe_mirror_lifetime` (tools/tests/coop_tribe_mirror_lifetime.py): up to 16 rounds of statehold + the game's removal of an NPC-tribe mirror's citizen + a reuse hunt in the guest's player tribe.
* FAIL (SPORE_FIX_OFF=tribe.mirror_member_lifetime): out/lab/20261112-010419 (the guest's new member at the freed block was driven onto the host's member #7).
* PASS: out/lab/20261112-012019 (the reused block was seen and the mirror made again, stale=1; no faults), out/lab/20261112-013003.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12: fixed on fix/w48-freed (tribe.mirror_member_lifetime).
- 2026-11-12: fix/w48-freed2: nine other users of a mirror's citizen still asked object_alive (faults on rel/0.7.13 in tribe_mirror_lifetime): BUG-0955; the test plays rounds until a reuse within 6 min.
- 2026-11-12: released in 0.7.13.
