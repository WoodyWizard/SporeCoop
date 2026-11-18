---
id: BUG-9017
title: The native host replacement probe destroys the conqueror's captured buildings
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo native host replacement checkpoint
role: host
versions: unreleased native prototype after duo9
fixed_in: unreleased
branch: duo-species
commits: 40964e6
tests: tools/tests/duo_tribe_rebuild_checkpoint.py
signature: old host hut and food mat absent after DestroyInstance with primary references retained
docs: docs/re/tribe.md#tribe-refounding-host-player
related: BUG-9015
---

## Report

The host replacement keeps three living original-species citizens and the
native player reference for 60 seconds, but the former hut and mat disappear.
The probe kept the old tribe's primary-building references until after calling
DestroyInstance, unlike the successful guest sequence which detaches them
before retirement. The conquered site's native owner fields alone were not
enough to preserve the buildings. No native faults occurred.
The native walking test also failed in that original control; its cause was
not isolated independently of the failed retirement sequence.

## Evidence

FAIL build/local/m2-host-identity-checkpoint.log: no native building at the
old site. The native hut/mat inventories each contain five objects, including
the replacement and excluding the captured old site. The observation log's
word "playable" covered population only; movement is a separate test. The
probe's output label has been corrected to "stable".

## Next check

Use the verified detach-before-retirement sequence. Keep a valid native
player tribe throughout the operation, then test actual controls, captured
buildings and cold loading together. This path is not in packaged Duo 9.

## Verification

FAIL: m2-host-identity-checkpoint.log, captured buildings disappear.
PASS: m2-host-detach-probe.log, the corrected operation keeps the survivor as
the native player during retirement, detaches primary references first, then
installs the new host tribe. Its population stays intact for 60 seconds.
PASS after native save, clean exit and restart: m2-host-detach-cold.log checks
both captured buildings, original species/faction, three living citizens,
native chief and a distinct new native object on both peers.
PASS: m2-host-detach-cold-orders.log checks native host selection, native
player reference and walking on both peers. This positive uses a checkpoint
reload; live control recovery without reloading remains unqualified.
PASS final compiled development build: m2-host-detach-final-checkpoint.log
and m2-host-detach-final-orders.log. Both peers exited with zero native faults.
