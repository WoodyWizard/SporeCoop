---
id: BUG-9052
title: Automated native Civilization camera tutorial does not acknowledge panning
status: not-reproduced
kind: bug
stage: Civilization
reported: 2026-11-05
source: Full native graduation test with first-use tutorials enabled
role: host
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/lineages_civ_camera_tutorial.py
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
2026-11-07: reproduced the console-input limitation on lineages25 in isolated
LineagesTutorialA. Tutorials remain enabled. Loading the old Civ checkpoint
alone did not reopen the lesson; the preserved five-victory Tribal checkpoint
was loaded into a new unused Game88, followed by normal City Hall and native
religious vehicle editors. The initial economic-role assertion was a fixture
mismatch; the actual requested editor was 9AD7D4AA and its normal UI completed.

Welcome to Civilization, Making Progress and Camera Tutorial were acknowledged
through their visible checkmarks. Holding both mouse buttons and moving the
camera advanced the rotation lesson normally. Holding VK_UP for two seconds
moved the camera but left the visible Use the arrow keys to pan the camera
instruction active, reproducing the earlier limitation. Both processes remained
alive. The Windows computer-use app-access approval timed out before native
Windows input could be tested. This still does not prove a player-facing defect;
no tutorial-skipping or gameplay patch was added.

The new --stop-at-arrival option on duo_tribe_graduation_cityhall.py is fixture
setup only: it stops at both native Civilization modes and does not claim
capital, design, tutorial or playable-HUD validation. Those checks must follow
the lesson. The initial run attempted capital validation while Welcome still
paused the world and failed; preserve lineages-tutorial-a-arrival-religious.log
as that test-driver failure, not proof of missing founder cities.

2026-11-07 follow-up: the native Windows surface could be listed, but activation
failed after refreshing its window handle. No additional approval is needed;
that tool could not provide the desired physical-key control.

A lab-only GetKeyboardState bridge received zero calls while the pan lesson
was active. It was unloaded and removed. The executable imports that API and
8D31B0 copies its 256 high bits into a caller buffer, but this does not establish
that the tutorial uses it. Do not add a GetKeyboardState gameplay/input patch
on that evidence.

LineagesTutorialA advanced from pan to zoom after moving the cursor to the
world center and sending repeated VK_UP down messages followed by key-up.
Normal wheel input then reached vehicle selection. A native selection box
selected the vehicle and opened the spice-geyser explanation. Acknowledging
that caption and the visible Goals checkmark released the tutorial pause.
Only then did the existing founder task assign the reserved capital its
original guest species. Earlier capital timeouts were during paused lessons.

lineages-tutorial-a-capitals-final.log passes both native species capitals;
lineages-tutorial-a-designs.log passes matching religious vehicle/City Hall
models; lineages-tutorial-a-controls.log passes guest purchase, native ownership,
shared charge and forwarded order. lineages-tutorial-a-quit.log records zero
faults, no probes and normal quits on both peers. No gameplay code changed.
A fresh first-use replay is being used to distinguish cursor placement from
key repetition before finalizing the test-driver diagnosis.

## Verification (2026-11-07)

LineagesTutorialC isolates the difference with the same centered cursor:
lineages-tutorial-c-pan-control.log FAILS after a single two-second held key;
lineages-tutorial-c-pan-repeat.log PASSES pan and zoom using repeated down
messages and a final key-up. Both variants use ordinary SporeKit input, with
no native state writes or temporary keyboard hooks. Together with the manual
TutorialA pass, the native lessons have advanced in two independent arrivals.
This closes the claimed gameplay blocker as not reproduced. The confirmed
automation limitation has a focused regression, not a gameplay workaround.

TutorialB used the old ten-second startup wait and hit the known black-screen
fixture issue. TutorialC's 600-frame wait exceeded Console's default 90-second
socket timeout during roughly 3-FPS menu rendering. The same processes later
finished the menu intro; loading then rendered normally. History's introductory
tooltip also required acknowledgement before its graduation check. Preserve
those setup failures separately; neither is a camera-regression pass.

TutorialC also completed native selection and Goals acknowledgement through
normal input. lineages-tutorial-c-finish.log passes both original-species
capitals and both graduation designs. The capture follow-up is tracked in
BUG-9088. Both processes later quit normally with zero faults and no probes
(lineages-tutorial-cd-quit.log); no new crash report appeared.
