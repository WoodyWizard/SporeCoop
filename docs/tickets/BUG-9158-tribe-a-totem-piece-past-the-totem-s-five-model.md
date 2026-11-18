---
id: BUG-9158
title: Tribe: a totem piece past the totem's five model slots crashes in Model::Release
status: open
kind: crash
stage: tribe
reported: 2026-11-12
source: qa
role: host
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, two games on the MSI 2026-11-12: coop.tribe win / wipe / totems)
signature: 0xC0000005 at Graphics::Model::Release+0xC (0x49F36C) read of 0x59 / 0x171, from Totem_AddSegment+0x76
docs: docs/re/tribe.md#tribe-totem-model-slots
related: BUG-9155 BUG-9030
switch: tribe.totem_full
---

## Report
QA 2026-11-12, two games on the MSI (Tribe, a test copy of a Tribe save): the host crashed twice in Graphics::Model::Release while a totem piece was added, once from the test command `coop.tribe win` and once from the game itself (TribeGame_ModeChanged, its reward queue) about 50 s after a party wipe's reload.

## Root cause
Observed (disassembly of 0xC8DDA0, `coop.tribe totems`): Totem_AddSegment makes the piece's model and assigns it to slot <pieces> of the totem's model vector (+0x108 begin, +0x10C end, +0x110 capacity) with the intrusive pointer assign 0x4792B0, without a bounds check, then appends the kind (+0x11C) and the colour (+0x130). The vector holds 5 slots (the stage's 5 wins). The test copy's totem had 5 pieces with 1-2 wins: the test command added a piece itself and the game added one more for the same queued win (TribeGame_ModeChanged 0xCDF162 calls the reward queue 0xCDAC90, which calls Totem_AddSegment), so each test win added two. The 6th piece released the word past the vector (0x131, 0x19) as a model. Inferred: a real party can reach it only when pieces and wins drift apart (a party wipe while a win's piece is still queued keeps the piece, BUG-9155's drop then skips).

## Fix
tribe.totem_full (on): a hook on Totem_AddSegment returns without a piece when the totem already has as many pieces as model slots (the win itself counts as before; logged). The test command `coop.tribe win` no longer adds a piece (the game adds it from the queued win). `coop.tribe totems` prints each founder village's pieces and slots. Off: as before (the crash on a 6th piece).

## Verification
FAIL: twice without the guard (crash-20261112-213055-i2, crash-20261112-214102-i2), the same save and steps. PASS: with the guard the same steps (a win, a party wipe, its reload, a win) logged "totem ... is full (5 pieces, 5 slots): no further piece" and ran on, 0 faults on both games. no-fail-run for the lab: a manual two-game run.

## History
- 2026-11-12: opened.
