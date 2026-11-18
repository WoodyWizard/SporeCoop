---
id: BUG-9070
title: Civilization launch vote leaves the spacecraft editor waiting on its own pause
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-06
source: Vantiegan cold ten-city shared victory
role: host guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_civ_launch_editor.py
signature: Civilization editor held now 1 after native History and completed spacecraft vote
docs: docs/re/civ.md
related: BUG-9056 BUG-9048 BUG-9060
---

Two independent nations own five cities apiece and the native readiness
check returns true on both peers after a cold load. History requests an
editor and the guest readies up. The vote closes but the host waits in Civ
with input_lock,paused and a held editor. Both games report zero faults.
civ-flight-after-ready.log and civ-flight-editor.log reproduce the failure.
Keep the real pause-menu and cinematic protections for unrelated requests.

civ-probe-editor.log identifies editor96B24187, blank model, return Civ
01654C04, context0116D51D, with input_lock,paused after the vote. The fix
checks this exact native request and the registered founder campaign's
native Space readiness. civ-fixed-editor.log and civ-fixed-live-state.log
show that the same vote now opens the spacecraft editor with zero requests
held and the general cinematic guard enabled.
The maintained regression passed independently in civ-warm-editor.log after
a 600-frame startup wait. The host saved a three-part ship through the native
editor and entered Space. Guest follow requires a separate orbital save.
