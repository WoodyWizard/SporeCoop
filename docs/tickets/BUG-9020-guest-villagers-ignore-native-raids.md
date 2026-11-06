---
id: BUG-9020
title: Guest villagers receive native raid orders but do not damage the target
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Duo native raid acceptance
role: guest
versions: unreleased after duo9
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_raid.py
switch: tribe.guest_ai
signature: native CitizenAction 15 works for host villagers but not guest villagers
docs: docs/re/tribe.md
related: BUG-9019
---

## Evidence

The target-ray correction delivers three native orders, but enemy hut HP
stays 750. Calling native DoAction directly reproduces the difference:
host citizens damage the hut (750 to 709), guest citizens do not. No damage
is injected. The guest village planner pointer is null, excluding planner
order replacement for this fixture. Citizen-only and whole-village political
high-bit probes did not help; all IDs were restored.

Native C8E660 (spatial this, plain ret) compares tribe identity to
GetPlayerTribe. Citizen spatial IsPlayerOwned C24590 delegates to its owner
tribe or city. Citizen setup C26E70 caches this result at AI+5FC bit 0;
host citizens have it set, guest citizens do not. Setting that cached bit
alone did not enable raids.

## Fix and verification

Recognize only the registered original-species guest village as player-owned
on the authoritative host, and refresh its citizen AI cache before ticking.
The campaign faction IDs and native player-tribe pointer remain unchanged.
The guest continues to mirror the host without running independent combat AI.

PASS build/local/m2-raid-acceptance.log, DuoQAM2RaidAcceptance4/5:
with tribe.guest_ai off, three native orders caused no hut damage over twelve
seconds; with it on, the same guest click caused actual native hut damage
and matching health on the other peer. Host citizens stayed outside the
battle area. Friendly-hut orders were rejected. Both native fault counters
were zero. The test cancels raid orders and returns attackers home afterward.
