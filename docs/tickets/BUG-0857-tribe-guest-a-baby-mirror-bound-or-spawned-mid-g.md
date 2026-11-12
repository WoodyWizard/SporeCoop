---
id: BUG-0857
title: Tribe guest: a baby mirror bound or spawned mid-growth does not follow the host's growth (age applied only at spawn) (suspected)
status: fixed
kind: desync
stage: Tribe
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages PR #196 BUG-9021
role: guest
versions: 0.7.3.6 .. 0.7.11.1 (code read)
fixed_in: unreleased
branch: fix/w48-tribe-civ
commits: 15b40763
tests: tribe_member_age_rejoin
signature: 
docs: docs/re/tribe.md#tribe-mirror-grow
related: BUG-0700
---

## Report
A baby member that grows up on the host may stay a baby on the guest for a while when the guest's mirror was spawned
or bound later than the host's baby was born (a guest joining or rejoining mid-growth). BUG-0700 found that a baby
the host makes while the guest is there grows up at the same second on both games.

## Root cause
Observed (lab 20261112-090138-2, `tribe.mirror_grow` off): the suspicion holds. The host's game made a baby; 12 s later
the guest left and joined again (9 s), its mirror of the baby was made or bound as a baby again; the host's baby grew
up at 59 s, the guest's mirror at 65 s (its own growth timer, from its making). tribe.cpp guest_state stored the
received age but used it only for a new spawn (`spawn_member(t, m.age)`); a bound mirror was never grown. BUG-0700's
normal case (the guest there at the birth) stays the same second.

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
tribe.cpp `tribe.mirror_grow`: in guest_state a bound live mirror that is still a baby (+0xB34 0) while the host's
member is an adult (age 1, not a body) grows up at once through the citizen's own GrowUp, cCreatureCitizen vt 0x78
(0xC248C0, plain ret, verified in Ghidra: the base growth 0xC223A0 sets +0xB34 = 1, the adult's model, scale and
health, the HUD's selectable member, the "grew up" event, clears the baby's hit sphere). Switch
`coop.fix tribe.mirror_grow off` (the mirror grows by its own timer only).

## Verification
`coop_tribe_member_age.py --rejoin` (suite `tribe_member_age_rejoin`, tribe fixture, ~2 min): the host's game makes a
baby (`coop.tribe spawn 1 0`), 12 s later the guest leaves and joins again; when the host's baby grows up the guest's
mirror must have age 1 within 3 s (the game's +0xB34 on both).
* FAIL with `SPORE_FIX_OFF=tribe.mirror_grow`: lab 20261112-090138-2 (host 59 s, guest 65 s).
* PASS: lab 20261112-090456-2 (both at 58 s, "mirror #8 grew up with the host's member"), 20261112-090653.

## History
- 2026-11-10: opened from the audit of fork PR #196 (plan 0.7.13).
- 2026-11-12 (fix/w48-tribe-civ): reproduced by a rejoin mid-growth, fixed (tribe.mirror_grow), FAIL and PASS runs in Verification.
