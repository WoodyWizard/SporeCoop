---
id: BUG-9055
title: Guest Civilization screenshot retains logo and trails after cold load
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Native guest capital UI visual check
role: guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests:
switch:
signature: black terrain repeated UI trails and Galactic Adventures logo
docs: docs/re/civ.md
related: BUG-9053 BUG-9054
---

DuoQACivControlA5: native city menu controls appear and respond, but the captured
frame shows black terrain, repeated UI trails and the startup logo. Host frame
is normal. Earlier CivDesignsA screenshots also retained the logo before the
new founder control hooks existed. Root cause is unproven; distinguish the
lab startup/load timing and camera state from a production rendering regression.
Do not count this as a visually passed gameplay test.

DuoQACivControlB waits for GalaxyGameEntry and 600 native frames before loading
or joining, following the upstream new_stage_game.py startup pattern. The guest
now renders the terrain and separate capital normally, with no logo/trails in
build/local/qa-stage-5.png after the native UI probe. This supports an early-load
startup race; it does not yet qualify a production fix for immediate joining.
