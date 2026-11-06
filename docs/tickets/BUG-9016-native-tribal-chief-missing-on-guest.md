---
id: BUG-9016
title: A village loses its native chieftain when its citizens are mirrored on the guest
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo rebuilt-village native checkpoint verification
role: guest
versions: 0.7.3.6-duo9
fixed_in: unreleased
branch: duo-species
commits: a0e7516
tests: tools/tests/duo_tribe_native_chiefs.py, tools/tests/duo_tribe_rebuild_checkpoint.py
switch: tribe.native_chiefs
signature: three mirrored native citizens but tribe mpLeader is null
docs: docs/re/tribe.md#tribe-refounding-retirement
related: BUG-9015, BUG-0381
---

## Report

After cold-loading the retired-settlement prototype, the host retains three
living citizens of species 0680BF38 and a native chieftain in faction 21F2.
The guest has the same three citizens and correct building ownership, but its
native tribe leader at +11C is null. The existing snapshot describes citizen
identity, age, ownership, transform and health, but not native leadership.
Replacing local citizens therefore cannot restore the host's native leader.

## Evidence

build/local/m2-retired-native-checkpoint-detail.log: host PASS, guest FAIL
with population=3 and chieftain=00000000. No native faults on either game.
The test reads native members, leader and building owners, not co-op chief tags.

## Planned fix

Transmit the host's native chief member ID and apply it only to a live,
mapped member of that local tribe after a complete snapshot cycle. Keep the
previous behavior behind a fix switch for the cold-join regression.

## Verification

FAIL: build/local/m2-chief-nofix.log, the guest switch disabled before joining.
PASS: m2-chief-positive.log, with the fix enabled, including a different host
chief. PASS: m2-chief-cold-positive.log, fresh process pair with default-on fix,
including an explicitly absent leader and a changed leader. PASS:
m2-chief-ko.log, a lethal hit leaves the chief dead with zero native health
on both games. PASS: m2-retired-native-checkpoint-fixed.log, both peers retain
the captured site's owner and three living original-species citizens with a
native chief. Zero native faults throughout. This fix is not in packaged Duo 9.
