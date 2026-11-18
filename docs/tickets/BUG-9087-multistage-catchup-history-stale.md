---
id: BUG-9087
title: Tribal returner loads Civilization but History still offers Enter Civilization
status: not-reproduced
kind: bug
stage: Space
reported: 2026-11-07
source: Targeted Tribal-to-Space returning-founder test
role: guest
versions: 0.7.3.6-lineages25-dev
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests: tools/tests/lineages_multistage_launch.py
signature: CivMode_SpaceReady true, ten team cities, History still Enter Civilization
docs: docs/re/space.md
related: BUG-9071
---

## Evidence

Isolated DuoQALineagesCatchupA uses EarlyLaunchB/Game0 as the orbital host
and FourWarriorsB/Game4 as the returning Tribal guest. Both retain founders
291489FF and 7CF26CCB. The normal join offers and loads the retained native
ten-city Civilization victory. Both nations own five cities; the native
CivMode_SpaceReady returns true. The queued orbital save correctly waits
for the guest's personal flight.

The actual History screen instead shows the Tribal cards and Enter
Civilization, so clicking its finish control returns to Civilization rather
than opening the spacecraft editor. lineages-catchup-a-launch.log fails
at the editor wait. Both native fault counters remain zero. The stale
history's source and lifetime are not yet established.

The initial Game2 fixture lacked the founder sidecar and was rejected before
joining; Game4 is the verified returning-founder save. Do not count that
fixture selection failure as a gameplay bug.

CORRECTION (2026-11-07): normal UI confirmation subsequently opened the
native spacecraft editor without changing any game state or production code.
lineages-catchup-a-finish.log passes personal ship creation, original-species
captain creation, Mission Control choices and reconnect with both identities
preserved. Both games exited with zero faults. The initial test had accepted
native city readiness while the arrival cinematic/History view still held
input. A stale-looking History view alone does not establish a broken launch.
The test now closes an already-open History view with its native checkmark
and waits for the cinematic and input lock to release before opening launch
History. A fresh complete replay is required before closing this report.

Do not investigate getter67DE50 as gameplay History: the dated correction
in docs/re/civ.md establishes that it returns telemetry. Simulator_GetUITimeline
at B3D510 is the verified no-argument getter (load singleton, plain ret).
Its live object already held Civilization at +14/+18 during this report;
those values do not prove the displayed event stream is refreshed.

## History

- 2026-11-07: reproduced the blocked native History action after normal
  Tribal-to-Space catch-up; investigating its native state.
- 2026-11-07: corrected the diagnosis. lineages-catchup-c.log passes the
  complete normal join from Tribe, retained Civ victory, own ship and
  original-species captain, Mission Control choices and reconnect. A/B
  also passed native launch/reconnect after correcting their UI drivers.
  No production patch was needed. The test must check visible caption
  ancestry, since History's root remains enabled inside the ship editor.
