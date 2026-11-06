---
id: BUG-9009
title: A guest refreshes a native tribe layout with a cleared species during unload
status: fixed
kind: crash
stage: Tribe
reported: 2026-11-03
source: Duo native refounding prototype in the Windows lab
role: guest
versions: 0.7.3.6-duo8 with manual native refounding experiment
fixed_in: 0.7.3.6-duo9
branch: duo-species
commits: 163046d
tests: tools/tests/duo_tribe_layout_exit.py
signature: dc04aee5 CE6AE7 reads null species +554, caller C98F0A, native mode GalaxyGameEntry
docs: docs/re/tribe.md#tribe-refounding-prototype
related: BUG-9007
---

After manually refounding the guest tribe on the host, the running guest had
to reconcile its prior village with the relocated one. Both reported zero
faults before quitting. During guest shutdown at 20:20:45 the native layout
code read tribe+18CC as null and passed it through CE6BC0 to CE6AE0. The latter
reads species dimensions at +554. The live mode was GalaxyGameEntry.

The immediately restarted guest also failed to copy its plugin shadow DLL
with Windows error 32 while the previous process finished its crash report.
A subsequent launch succeeded. The cold host and guest then both restored
the old hut and food mat under the conqueror and the new settlement under
the original guest faction, with its original species and zero faults.

The crash evidence is crash-20261103-202045-i5.txt and its matching dump in
the disposable lab. Refounding is still a native experiment and has not
been integrated into the playable fork.

The next-build source guards native C98940 before it reads a cleared species
profile. tools/tests/duo_tribe_layout_exit.py retains a real tribe, leaves the
stage, confirms native cleanup cleared its species, and makes the same late
layout request for kind 12. The guarded run passed with zero faults. With
tribe.layout_profile off, it reproduced CE6AE7 reading address 554 at 20:34:39.
The source is checkpointed for Duo 9. The packaged preview remains Duo 8.

## Verification

- PASS build/local/duo9-layout-exit.log, native stage cleanup and the late
  layout request on instance 5 with the guard enabled, zero faults.
- FAIL build/local/duo9-layout-exit-nofix.log, the same native sequence with
  tribe.layout_profile off, CE6AE7 reading 554. Matching native crash evidence
  is crash-20261103-203439-i5.txt in the disposable lab.
