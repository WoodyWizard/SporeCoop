---
id: BUG-1316
title: The guest's own Tribal Outfitter result was not taken: its return hook compared the result's +0x14 (the model type) with the outfitter's editor id, so the game reloaded the shared slot with the guest's outfit
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
fixed_in: 0.7.16
branch: fix/w57-own-editors
commits: 6b93ebd6 5e4f14e1
tests: tribe_own_outfitter, own_editor_drop
signature:
docs: docs/re/tribe.md#tribe-own-outfitter
related: BUG-1272, BUG-1298, BUG-1308
switch: tribe.own_outfit_result
---

## Report
Lab 20261119-111652 (fix/w57-own-editors, after BUG-1308): tribe_own_outfitter came back from the guest's own
outfitter, but `coop.designs` said `returns=1 skipped=0`: the guest's tribe slot was reloaded with the guest's new
outfit 2928F397 on its game (every member there, the host's too, dressed in it until BUG-1156's 2 s check put the
host's outfit back), the line never got the outfit, its members kept the creature's look on both games.

## Root cause
✔ (code + docs/re/civ.md#civ-own-editors) designs.cpp h_EditorReturn took the result only when its +0x14 was the
outfitter's editor id 0x156276D1. The editor's result (OnExit 0x587C51, 0x48 bytes) has at +0x14 the editor model's
+0x58, the model type (house 4E3F7777 for the Civ house editor; for the City Hall the id 0x99E92F05 happens to be
both), not the editor id. So the result was never ours and 0xCDCC90 reloaded slot 0 with it.

## Fix
`tribe.own_outfit_result` (designs.cpp): our pending request, the Tribe tag 0x116DD1B at +0xC and the outfitter's branch
of 0xCDCC90 (+0x28 / +0x29 clear) name the result; +0x14 is logged ("our own Tribal Outfitter returns: result tag
... type ...") and shown as `rtype=` in `coop.designs`. Off: +0x14 must equal the editor id (as before: never taken).

## Verification
FAIL: fix/w57-own-editors worktree out/lab/20261119-111652 tribe_own_outfitter ("the guest's return skipped the slot reload (skipped=0)", the slot
reloaded with 2928F397); twin fix/w57-own-editors worktree out/lab/20261119-130049 tribe_own_outfitter_result_off FAIL the same (skipped=0, the guest's
slot 29291B69). PASS of every outfit check: fix/w57-own-editors worktree out/lab/20261119-113002 and -130049 tribe_own_outfitter (skipped=1, the slot
the host's, both games dress the guest's members in 954ADE35, log "result tag 0116DD1B type 372E2C04"),
own_editor_key PASS 20261119-130049. tribe_own_outfitter's last check (the host's save) is BUG-1326.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-own-editors, lab runs above.
- 2026-11-23: released in 0.7.16.
