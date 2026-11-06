---
id: BUG-9038
title: Guest Tribal alliance does not advance the shared campaign
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Native two-performance qualification
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance.py --alliance, tools/tests/duo_tribe_alliance_checkpoint.py
signature: NPC-to-guest native relationship7 but shared totem remains1
docs: docs/re/tribe.md
related: BUG-9035 BUG-9037 BUG-9025
---

m2-performance-completion-a.log: ordinary guest keys win two native musical
performances. Relationship progresses0 to3 to7 on both peers, preserving the
guest species. An ordinary guest walk between performances bypasses BUG-9037.
The second win does not increment either world's shared native campaign totem
within20 seconds; both retain one segment. Both processes exit with zero faults.

The native-earned relationship is proved. Shared goal completion, totem history,
tool unlocks and checkpoint delivery still need integration and qualification.
Reuse native goal/reward paths rather than manufacturing a totem or an unlock.

Root cause: native goal C2F750 and completion C2F7F0 evaluate NPC-to-native-player
relationships. The guest's distinct faction earned the win, but the shared
campaign's native faction did not receive that relationship. Completed native
sessions now share their earned relationship with the host campaign faction,
using native success and ApplyRelationship. The original goal system supplies
the reward. No score, goal completion, totem segment or unlock is written by
the adapter. Callbacks retain scalar identities and revalidate world/factions.

The checkpoint observer also detects native totem segment changes, since a
victory may unlock no new tools. Saving waits for native result cinematics.
AllianceCreditA earned the host segment but its test failed to acknowledge the
victory screen; that failure was test setup, not a failed transfer. ResumeA
acknowledges the observed native Continue controls and passes both-peer credit.

PASS m2-alliance-resume-a.log: two actual guest keyboard performances earn
relationships0/3/7, native campaign history gains the alliance segment, and
the confirmed host save reaches the guest. PASS m2-alliance-cold-a.log: fresh
processes retain segment types[0,1], identical native colors, level1, unlocks
[1,2,3,4,5,6,7], both original species/factions and NPC alliance8674 with both
founders. Both pairs exit with zero native faults. Negative evidence is
m2-performance-completion-a.log / m2-performance-repeat-b.log before this fix.
