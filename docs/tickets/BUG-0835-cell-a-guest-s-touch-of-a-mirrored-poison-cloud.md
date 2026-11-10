---
id: BUG-0835
title: Cell: a guest's touch of a mirrored poison cloud counts as eating it, so the host's cloud ends early (suspected)
status: open
kind: desync
stage: Cell
reported: 2026-11-10
source: code audit 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs: 
related: BUG-0716 BUG-0636
---

## Report
No player report. Suspected: a guest swimming through a mirrored poison cloud (an NPC's, the host's, or its own relayed one) makes the host remove that cloud.

## Root cause
Code reading (line numbers of main e49e1e23 = 0.7.11.1): h_Destroy (cellsync.cpp:1144-1174) treats any DestroyCell of a mirror touched in the last 1 s as eaten and sends ACT_GONE; the host then calls o_Destroy(idx) (4159). The guest's visibility step (return address 0xE78C37, docs/re/cell.md) destroys poison cells outside its active area every frame; once the avatar had a pair with one (g_touched, h_Pair 1549) the next such destroy is sent as eaten. Not known: whether mirrors (group -1) and the avatar form pairs with clouds. Confidence: low-medium.

## Fix
Not fixed yet.

## Fix idea
In h_Destroy never count the poison-cull return address or type-5 (poison) cells as eaten.

## How to test
Host `coop.pool borrow poison graft`, guest swims through the host's clouds; guest `coop.pool why` must show no touched destroy from 0xE78C37 sent as gone, host no 'gone ... applied' on poison cells.

## Verification
Not fixed yet.

## History
- 2026-11-10: opened (code audit 2026-11-10 (0.7.13 plan), plan/w47-cellcreature).
