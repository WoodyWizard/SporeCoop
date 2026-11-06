---
id: BUG-9051
title: Guest mirror retains a stale citizen while the host edits during graduation
status: fixed
kind: crash
stage: Tribe Civilization
reported: 2026-11-05
source: Native graduation vehicle edit and host return
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch: tribe.mirror_member_lifetime
signature: remove_member reads stale owner E89E7FE2 when host returns
docs: docs/re/tribe.md
related: BUG-9050 BUG-9046 BUG-0081
---

DuoQAGraduationSaveA host reached native Civilization. Guest crashed when the
host briefly returned to Tribe after vehicle editing, before the stage handoff.
Exact deployed DLL RVA1889AE resolves through llvm-addr2line to remove_member
tribe.cpp1896, called by guest_tick2348 and tick4236. The mirror's saved pointer
passed object_alive (only a flag read), but its owner was E89E7FE2. Native local
play had continued while the host was in the editor. Report
crash-20261105-025826-i5.txt. Validate cached mirrors against native current
membership before using them, and never call RemoveMember through a stale owner.

Dedicated lifetime regression: tribal-graduation-lifetime-final.log removes
one guest-local member through native RemoveMember while the host remains in
the vehicle editor. It verifies absence from the live member vector, holds
the editor for 90 seconds, then completes the native design and transition.
Both games reach Civilization, share the actual custom designs and retain the
original founders, with zero faults. tribal-graduation-capitals-final.log
checks both species' native capitals; tribal-graduation-lifetime-quit.log
records fault-free clean exits. The deliberate local removal is a lifetime
fixture, not an unassisted soak or a claim that every freed-address reuse was
exercised. The original observed crash remains the negative evidence.
