---
id: BUG-9084
title: Guest native religious conversion meter stays at zero
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-07
source: Targeted religious city takeover checkpoint
role: guest
versions: 0.7.3.6-duo23-dev
fixed_in: 0.7.3.6-lineages24-dev
branch: feat/lineages-civ-combat
commits: c2c6d0d5
tests: tools/tests/lineages_civ_religious_state.py
signature: Host rival convert=100 while guest convert=0
docs: docs/re/civ.md
related: BUG-9077 BUG-9081
---

## Evidence

The installed Duo 23 DLL on isolated DuoQAPlayabilityReligionM receives the
guest's actual religious city order on the host. A prepared follower map has
28 of 30 followers for the guest. The host computes convert=100, while the
guest remains at convert=0. Existing snapshots send military and economic
state, not religious follower state. Evidence: playability-religion-m-state.log.

The lone preacher died before final capture. That run does not establish a
capture bug; the targeted fixture needs enough preachers to survive the wall.
No native faults occurred. Do not read its removed vehicle pointer afterward.

## Fix

Replicate the full native unsigned-key follower map, converter and meter in
a bounded host snapshot. Apply on the guest outside the frame after validating
world, epoch and live city identity. Remove stale map entries through the
native scalar-node erase and insert through CityFollowersMap_Index. Preserve
host-only conversion, capture and progression. Switch: civ.religious_state.

## Verification

FAIL: build/local/bug9084-before.log, installed Duo 23, native host three
guest followers and convert=6.666667; guest none and convert=0.

PASS: build/local/bug9084-pass-n1.log and bug9084-pass-n2.log, Lineages 24.
Both compare native maps, converter and meter, changed counts, and deletion
of a stale high-bit faction entry. The second reconnects into a fresh guest
slot and preserves the founders. Both games report zero native faults.

These results qualify religious state replication, not an autonomous siege.
The separate control test still investigates preacher loss before capture.

## History

- 2026-11-07: reproduced on the old build, implemented native follower snapshots,
  passed twice including reconnect and related economic-state regression.
