---
id: BUG-9052
title: Automated native Civilization camera tutorial does not acknowledge panning
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Full native graduation test with first-use tutorials enabled
role: host
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch:
signature: camera moves through console input but pan tutorial remains paused
docs: docs/re/tribe.md
related: BUG-9046
---

m2-graduation-full-a.log reached native Civilization on both clients with zero
faults, one native capital, retained host species and both founder records.
Its final busy:- assertion timed out at the first-use camera tutorial. Native
tooltips were acknowledged manually. The two-button camera rotation step
advanced; all arrow directions and WASD moved the camera, but the next native
panning instruction remained paused. SporeKit input already overrides
GetKeyState/GetAsyncKeyState. A mod defect versus an automation-input limitation
is not established. No production workaround is justified yet.
For repeatable campaign handoff qualification, fresh disposable profiles may
set their existing OptionTutorialsEnabled preference to0 before launch.
That fixture does not qualify the first-use camera tutorial. User preferences
are untouched. Real-input tutorial behavior remains to be checked separately.
