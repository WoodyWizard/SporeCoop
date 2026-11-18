---
id: BUG-9088
title: First-use capture regression leaves Communications Panel tutorial unanswered
status: not-reproduced
kind: bug
stage: Civilization
reported: 2026-11-07
source: Targeted post-tutorial capture regression
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in:
branch: feat/lineages-civ-combat
commits:
tests: tools/tests/duo_civ_team_progress.py
signature: native capture checkpoint waits while Communications Panel lesson is visible
docs: docs/re/civ.md
related: BUG-9052 BUG-9057
---

## Evidence

lineages-tutorial-c-team-progress.log times out after the first native fixture
capture. Both clients display the first-use Communications Panel lesson and
the world is paused. The old driver acknowledges cinematic captions and city
specialty choices, but not this introductory tooltip. This does not demonstrate
a campaign-save failure or reproduce BUG-9057.

The new acknowledge_capture_tutorial helper requires the visible, specifically
named lesson before clicking its checkmark. It never acknowledges an unknown
OK box or a save confirmation. lineages-tutorial-c-capture-ack.log executes
that helper against the existing prompt on each peer and verifies it closes.
Both native nations then converge to captures=(1,0,0), highest=3, with distinct
species and city counts 1/2. No gameplay code or progress value was patched.

The original run remains a failure, not an uninterrupted pass. A resumed
second capture and a fresh regression are separate validation steps.

## Follow-up verification

lineages-tutorial-c-team-progress-resume.log passes the second capture and
native final checks after acknowledging the missing lesson. This is an
assisted continuation, not a replacement for the failed initial run.

The two-capital TutorialA checkpoint was copied to a new unused Game89 in
TutorialC's isolated host profile. A normal load and rejoin restored both
peers. The first attempted test started before founder ownership rebound;
lineages-tutorial-d-team-progress.log preserves that precondition failure.
The driver now waits up to 30 seconds for the two native owner bindings.

lineages-tutorial-d-team-progress-ready.log passes both captures uninterrupted
with the capture-tutorial handler present. On both peers, each founder owns
two cities, technology is 3, highest team city count is 4, unlock bytes match,
and both original species remain intact. These are native fixture captures,
not player-earned combat or a full-stage replay. The ownership-wait addition
was made after this run and syntax checked; that new wait was not exercised
on another fresh load.

lineages-tutorial-cd-quit.log records zero faults, no probes and normal quits
on both peers. No new crash report was written. This qualifies first-use
capture progression with the existing save watcher; it is not the missing
switch-off control for BUG-9057.
