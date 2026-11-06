---
id: BUG-9035
title: Guest village cannot start native Tribal performances
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Two-founder Tribal social command audit
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance.py
signature: village allowlist excludes CitizenAction26
docs: docs/re/tribe.md
related: BUG-9033 BUG-9025
---

Separate-village commands rejected native social action26. The guest now sends
the ordinary performance order and instrument keys, with host validation of
ownership, live session token, world, equipped tool and native cooldown.
Host snapshots expose native requests/progress on the guest. Spore owns scoring
and relationship rewards; no score, alliance or campaign reward is fabricated.

FAIL control in m2-performance-band-e.log: disabling tribe.performance rejects
the order. PASS with the fix: guest keys reach native progress1.0, NPC-to-guest
relationship changes0 to3, both peers converge, original species remain and
both instances exit with zero native faults. Earlier PerformanceA found the
host-only roster guard in guest keyboard input; village_orders() fixes it.

This establishes a native friendly performance win. The second win, alliance
campaign rewards and F9 mouse-start path need their own end-to-end qualification
before claiming complete Tribal alliance gameplay. Build and symbol lint pass.
