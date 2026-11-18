---
id: BUG-9057
title: Save confirmation can outlive its watcher and mask a new save
status: open
kind: bug
stage: Civilization
reported: 2026-11-05
source: Native shared campaign regression
role: host guest
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests:
switch: world.saved_box_only
signature: old saved box accepted as proof of a new campaign checkpoint
docs: docs/re/civ.md
related: BUG-9056 BUG-0276
---

During m2-civ-team-progress-fixed-a.log the second capture's technology caption
overlapped a saved confirmation. The watcher logged confirmation but the box
remained visible past its 30-second lifetime. After manual acknowledgement,
the next checkpoint accepted a visible saved box after 47 ms and transferred
old campaign data: host capture count two, guest one. Both games stayed alive
with zero faults. The campaign regression failed convergence.

Do not accept an old save confirmation as evidence that a new save completed.
Do not post a campaign save after SetVehicleTech starts a new native caption.
Qualify automatic confirmation, actual file freshness, and native campaign
counters after transfer. No manual save-button acknowledgement in the passing
regression. Preserve ordinary caption acknowledgement as player input.

CORRECTION (2026-11-05): the first stale-box interpretation was incomplete.
The native captured-city specialty prompt also uses window 06318E78. In run C
it contained choices 05107B19 (Religious) and 05107B1A (Military), not saved
confirmation OK FFFFFFF1. That unanswered choice explained the pending second
checkpoint. Run A's 47 ms observation does not prove an old save box: this
shared identifier can misclassify the specialty question as save completion.
The watcher must require an OK button and exclude both specialty choices.
The test must answer the actual specialty question, without acknowledging
save boxes itself. Native file freshness remains a separate verification need.

Run D passes the complete native progression regression without manual input:
the driver answers caption and specialty choices only, both automatic campaign
checkpoints converge, no dialogs remain, both processes report zero faults.
The isolated switch-off reproduction for the watcher-specific correction is
still pending, so keep this ticket open rather than claiming full qualification.

## Cell recurrence at 800×600

On 2026-11-08, a two-PC Cell host finished its first world transfer while the
native saved-game confirmation remained open. The next guest joined, but no
transfer offer appeared for 78 seconds. The host explicitly logged that it
was waiting for the previous save confirmation. The confirmed dialog extended
past the right edge of the 800×600 QA window. Pressing Enter with that dialog
visibly present closed it. This is a separate obstruction from slow UDP
delivery. The observed offscreen dialog does not establish why the game
positioned it there or how often it affects normal player setups.
- 2026-11-15: the 0.7.14 merge folded this switch into upstream's world.saved_box_only (BUG-0846).
