---
id: BUG-9026
title: Delayed native online-service error blocks Tribal camera tests
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Native conquest qualification
role: test harness
versions: duo10-dev
fixed_in: duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_raid.py --conquest
signature: delayed offline-service modal intercepts camera keys
docs: docs/re/tribe.md
related: BUG-9025
---

m2-conquest-goal.log failed before the enemy raid because a delayed Spore
service-error dialog intercepted camera input. Screenshot qa-stage-5.png and
native UI tree identify caption 0451D381 and close button FFFFFFF2. Registration
preflight alone cannot catch an error arriving later. The Tribal camera driver
now closes this specifically identified dialog while panning, with visibility
checks before and after. No login, network-setting change or unknown dialog
acceptance is performed. Qualification pending.

PASS m2-conquest-goale.log: camera setup and native raid completed with this
handling enabled. The gameplay outcome is independently asserted on both peers.
