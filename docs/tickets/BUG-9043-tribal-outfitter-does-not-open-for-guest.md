---
id: BUG-9043
title: Tribal outfit vote never opens the guest species editor
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Native outfitter from live two-species campaign
role: guest
versions: duo10-dev
fixed_in: unreleased
branch: duo-species
commits: 336e7e8
tests: tools/tests/duo_tribal_outfitter.py
switch: tribe.own_outfitter
signature: MSG_VOTE own-editor scheduling accepts Creature only
docs: docs/re/tribe.md
related: BUG-9040 BUG-0404
---

DuoQAOutfitterA4/5 are fresh copies of the successful live transition.
Click the native host hut, dismiss the Tribal Planner tutorial, choose its
hat tab and the portrait's Edit Outfit button. Both players accept the vote.
The host eventually enters editor156276D1 with its species291489FF. The guest
stays in Tribe with no editor request and no own editor due.

The host waits60 seconds on the planner's own input_lock/paused state before
the editor gate expires. This is a delay, not an indefinite deadlock.
MSG_VOTE schedules own editors only for Creature/landfall; open_own_editor
requires a Creature avatar, and edited-creation adoption only handles Cell
and Creature. Adding only the vote condition would not implement clothing,
native village appearance, ownership, save/reload or reconnect lineage.

Next: reproduce native host save behavior, then preserve immutable founder
identity separately from an edited model key. Retain shared campaign unlocks
and reject foreign species changes. Do not call the unrelated old-build
D5BFA0 address by assuming a fixed address shift.

2026-11-05: guest native editor entry implemented with TribalGame+19C items,
the five shared native consequence traits and its own species/name. Automated
entry regression FAILED with tribe.own_outfitter off (m2-outfitter-control-a.log,
guest never opens) and PASSED on (m2-outfitter-entry-a.log), distinct291489FF /
29148A01 and identical46 clothing parts,23 locked,23 available hash8FFC4D2C.
Both games reported zero native faults and exited normally. This qualifies
entry only. Guest outfit adoption, native village application and checkpoint
lineage are under development, not yet a completed fix.

Outfit history Windows unit checks pass: original DUO1 bytes preserved, new
appearance versions survive readback, stale/foreign founders rejected, blocked
atomic replacement retains prior bytes, corrupt history refused, and Civ
registration accepts recorded descendants while rejecting unrelated species.

Normal path implemented and qualified: m2-outfit-final-b.log passes native
hat edits, both saved model versions, unchanged original founders, all six
native citizens, guest walking on both peers and zero native faults. The
native host result can arrive after the guest result; a settling guard avoids
overlapping checkpoints, but two sequential refreshes remain possible.
Cold m2-outfit-cold-b.log passes the saved pair after process restart. Keep
this ticket open until editor cancel/interruption and reconnect are qualified.

Cancel-b passes both native red-cross exits, unchanged291489FF/7CF26CCB
models and original identities, three citizens each, native ownership/walking
on both peers and zero faults. Cancel-a failed to open the vote from coordinate
input, before reaching cancellation; it is not a pass. BUG-9044 covers the
interrupted adoption and resumed-editor defect found during this qualification.
Its clean leave, hard process exit, normal acknowledgement and subsequent edit
tests pass. The normal, cancel, interruption and cold-load paths are qualified
locally; real two-PC testing remains outstanding for the full baseline.

## Verification

FAIL build/local/m2-outfitter-control-a.log (guest editor never opens).
PASS build/local/m2-outfitter-entry-a.log and m2-outfit-final-b.log (entry,
native saved appearances and walking). PASS m2-outfit-cold-b.log (restart).
PASS m2-outfit-cancel-b.log (native cancel and walking).
