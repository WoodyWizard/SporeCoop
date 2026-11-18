---
id: BUG-9025
title: Guest village conquest does not credit shared Tribal progression
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Native guest raid acceptance
role: host and guest
versions: duo10-dev
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_raid.py --conquest
switch: tribe.conquest_refresh
signature: native guest conquest credits host but guest keeps stale village, totem and unlock records
docs: docs/re/tribe.md
related: BUG-9020 BUG-9023
---

Reproduced on disposable DuoQAConquestA4/5 using a guest F9 Raid order.
The fixture reduces enemy hut health to 15 without defeat; ordered native AI
delivers the lethal strike. Host fighters remain distant. Host native tribe
defeated flag becomes 1, guest remains 0, native unlocks remain (1,5) on both.
Both processes report zero caught faults. Log: m2-conquest-control.log.

The reproduction establishes a failure; the root cause is under investigation.
Compare the ordinary host conquest path before choosing an adapter.

CORRECTION (2026-11-04): the same checkpoint also fails to credit a host
conquest (m2-conquest-host.log). Therefore missing unlocks are not established
as guest-specific. The rebuilt host has no native totem, whereas the earlier
DuoQAHost checkpoint retains one. This is evidence to investigate campaign
state preservation during refounding, not proof of its exact root cause.
The guest's stale defeated flag remains independently reproduced.

CORRECTION (2026-11-04): the original raid target 21E2 had no active native
campaign goal. The active goal referenced 21E0/species 06785BC2. Defeating that
target on the earlier checkpoint with its native totem immediately grants one
segment and unlocks (1,3,4,5,7), both for a host raid and a guest F9 raid.
Host log: m2-conquest-active-goal-host.log. Guest control:
m2-conquest-guest-goal.log. The latter fails because the guest still has
unlocks (1,5), zero segments and an undefeated village. Native SetDefeated
alone on that guest does not grant the missing rewards
(m2-conquest-native-mirror.log). No new reward calculation is needed.

The patch observes native enemy defeat and unlock changes, waits for a quiet
interval and completed cinematic, then reuses the native saved-world transfer
to the connected guest. The host remains in the running world. Absent players
receive the campaign on reconnect. Qualification is in progress.

CORRECTION (2026-11-04): even target 21E0 has no goal until native input
registers it. Guest orders bypassed CD0DAB's call to goal UI SelectTarget
CD3D70. The fix queues that same idempotent native registration outside the
frame after validating the guest's raid. It resolves the target again by
faction and rejects callbacks from a different world epoch. The campaign
observer runs twice per second and waits for result cinematics before saving.

PASS m2-conquest-goale.log on disposable DuoQAConquestGoalE4/5: friendly raid
rejected, native guest AI delivers the lethal hut strike, host unlocks change
from (1,5) to (1,3,4,5,7), both result screens acknowledged, native save
transfers and reloads on guest. Both worlds retain the ruined hut and exactly
one totem segment, with matching native tool permissions and zero faults.
The fixture supplies food and nonlethally reduces hut health to one; this
qualifies reward completion and synchronization, not full battle balance.
Earlier failure evidence: m2-conquest-refresh.log (missing goal),
m2-conquest-guest-goal.log (missing guest state). Harness failures and fixes
are recorded separately in BUG-9026/9027. Host-rebuild totem retention remains
separate work, so this does not establish complete Tribal progression.
