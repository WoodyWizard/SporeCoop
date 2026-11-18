---
id: BUG-9048
title: Native Tribal graduation pauses while the co-op editor gate waits for it to resume
status: fixed
kind: bug
stage: Tribe Civilization
reported: 2026-11-05
source: Live five-victory native History graduation
role: host
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_graduation_editor.py
switch: tribe.graduation_editor
signature: editor99E92F05 held at input_lock,paused after ready2/2
docs: docs/re/tribe.md
related: BUG-9046 BUG-0404 BUG-9043
---

After five earned native victories, History and the native graduation caption
request editor99E92F05. Both players ready at s602.535. The host remains on
black with input_lock,paused and no active cinematic, while the generic editor
gate waits for the native transition to resume. At s650 the request is still
held, with zero faults. Do not remove the global cinematic or pause guard;
identify this native request and preserve real pause/dialog protection.

Verification:
- FAIL build/local/m2-graduation-control-a.log with tribe.graduation_editor off:
  ready2/2 completed but native City Hall still blocked after12 seconds.
- PASS build/local/m2-graduation-fixed-a.log on fresh profiles: same native
  actions open editor99E92F05 promptly, generic cinematic guard enabled,
  zero faults on both games. Both processes exited cleanly after this check.
- This test stops at editor entry. City Hall completion is tracked separately
  in BUG-9049 and full separate-nation handoff remains BUG-9046.

Final candidate replay: tribal-graduation-final-editor.log passes native
History, ready vote and City Hall entry with the generic cinematic guard on.
tribal-graduation-lifetime-final.log then completes both native design editors
and reaches Civilization on both games. Clean exits, zero recorded faults.
