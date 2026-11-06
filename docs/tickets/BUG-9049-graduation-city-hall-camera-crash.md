---
id: BUG-9049
title: Graduation dereferences an absent animal pen after saving City Hall
status: fixed
kind: crash
stage: Tribe Civilization
reported: 2026-11-05
source: Disposable native five-victory graduation probe
role: host
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_cityhall.py
switch: tribe.graduation_pen
signature: CDD466 null read via SimCamera_Update+806 after City Hall editor
docs: docs/re/tribe.md
related: BUG-9046 BUG-9048
---

Host DuoQATribeBudgetFixedB4 saved its three-part City Hall design. The game
returned from Editor to Tribe at s965.779, paused, then crashed at s967.182.
Runtime FCD466 is canonical CDD466 (module base6F0000, delta2F0000), null data.
Calls include canonical B14296 (SimCamera_Update+806), CDECAC and CDF11C.
Crash report crash-20261105-023757-i4.txt. Guest has zero faults.
This followed diagnostic disabling of core.editor_cinematic to release the
native graduation request; the guard was reenabled before saving the design.
The new scoped graduation patch was not loaded in this run. Diagnose the
native camera target/lifetime before claiming the transition works.

CORRECTION (2026-11-05): CDD466 is not a null camera object. Decoded CDD461
calls C90060, whose verified body returns tribe+19C4 (animal pen), then calls
virtual54(0) on it without a null check. The community SDK cTribe.h names this
field mpDomesticatedAnimalsPen. All seven native tribes in the restored fixture
have null pen and egg-pen pointers while in the editor. The failing sequence
hides village assets for graduation. A guard skips only its eleven-byte virtual
call when the pen is null; non-null uses the original trampoline. Installation
requires an exact byte match, and the naked stub preserves registers/flags/stack.
Source: https://raw.githubusercontent.com/Spore-Community/Spore-ModAPI/master/Spore%20ModAPI/Spore/Simulator/cTribe.h

Final qualification: tribal-graduation-lifetime-final.log passes both native
design editors, including a 90-second vehicle-editor hold, reaches native
Civilization with matching custom design resources, and records zero faults
on both games. tribal-graduation-capitals-final.log confirms both original
species' separate native capitals. tribal-graduation-lifetime-quit.log records
zero faults and clean exits. Earlier pre-fix crash is the negative evidence.
