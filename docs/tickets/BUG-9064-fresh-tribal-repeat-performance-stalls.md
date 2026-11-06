---
id: BUG-9064
title: Repeat performance stalls on second NPC in fresh alliance campaign
status: open
kind: bug
stage: Tribe
reported: 2026-11-05
source: Fresh Vantiegan low-frame-rate native alliance qualification
role: both
versions: duo10-dev
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance.py
signature: second rival relation3 nine action26 performers no snapshot within45s
docs: docs/re/tribe.md
related: BUG-9037 BUG-9063
---

In the untouched Vantiegan campaign the guest earned the first NPC alliance
with the starter instrument. Both peers recorded the same victory and four
unlocks. The guest then earned relation3 with the second rival, faction21CF.
The next action26 targeted its correct chief (network member14); no guest
performance snapshot appeared in 45 seconds. The host diagnostic shows the
last group order with nine members and the intended chief. Both games were
alive at 10 FPS with zero recorded faults. Preserve the failure. It is not
proof that the 10 FPS cap caused it or that the original BUG-9037 returned.

Unlike the earlier first-rival fixture failure, the post-timeout screenshot
shows normal gameplay and neither peer reports a vote, a visible Continue
button or a performance session. No capture of the entire interval rules out
a transient prompt. Determine whether the delayed order reached each native
AI and what replaced or prevented its action before changing repeat timing.

Evidence in build/local: tribal-friendly-c-campaign.log (first alliance,
second NPC initial win and repeat timeout), tribal-friendly-second-stall.log,
tribal-friendly-earned-one.log, tribal-friendly-checkpoint-save.log and
tribal-friendly-checkpoint-quit.log. Disposable DuoQATribalFriendlyB4/B5
retain the earned checkpoint. Reload can clear transient AI state: a replay
from that checkpoint is useful but is not an exact restoration of the order.

2026-11-06: the saved second rival reached relation7 on both peers on the
first cold replay. Host traces show action26 targeting its actual chief and
both native performer and audience trees activating. The next three rivals
also supported consecutive native performances. A separate unsent order after
a checkpoint is reproducible and fixed under BUG-9065. Do not attribute the
original host-accepted timeout to that missing send. Keep this case open
until the failed interval is captured or an equivalent control is identified.

The host now records each performance order's sequence, target, selected
member count, native dispatch count and non-null returned action count.
six-native-five.log captured twelve returned actions for each of four orders,
including two repeats. The aggregate command counter also includes instrument
inputs and alone cannot establish that a particular repeat reached native AI.

Six-cold-six.log and six-repeat-six.log reproduce a cold-save stall against
the final original rival. In six-repeat-ai.jsonl the guest chief changes
from a prior action26 on chief36 to a new action26 targeting chief45, then
enters native eating node0461300F. Nine of twelve selected musicians are
in that node with the correct target still queued. No epic spawned and all
twelve returned native action records. This explains the blocked band on
this save. The original second rival remains an independent report.

On the same five-alliance checkpoint the native Eat control consumed nine
food and the band left the eating node (six-fed-meal.log). All twelve then
earned both performances and the sixth alliance (six-fed-six.log). No score
or relationship was set and no native hunger behaviour was suppressed.
