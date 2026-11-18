---
id: BUG-9125
title: Cell: meat a partner ate stays on this screen at a huge size
status: open
kind: bug
stage: Cell
reported: 2026-11-11
source: owner report on 0.7.8-lineages49-dev
role: host (possibly guest)
versions: 0.7.8-lineages49-dev
fixed_in:
branch:
commits:
tests: -
signature: -
docs: -
related: BUG-0364
switch: none
---

## Report
"When meat is eaten by the connected player (possibly vice versa), the host sees it still but massive size."

## Findings (not reproduced)
QA Cellfive.spo host 9 + guest 8: the guest ate ambient meat c2272067774 (`coop.pool swimto` + `coop.pool eat`,
12 points): gone on both games within 1 s. The host's own eat of another chunk: gone on both. Not tried: meat from
a kill (loot), a chunk bitten off a big cell (EatChunkA/B, MSG_CELL_HIT type 6), meat bigger than the eater.

## Two-PC attempt (2026-11-11, not reproduced) and diagnostics
TITAN hosts Cellfive.spo (QA, instance 9) + a QA guest; the MSI joins over Tailscale (3 players). The MSI killed a
creature (`coop.pool kill`) and ate a loot chunk (`coop.pool swimto <key> tp`, `coop.pool eat <key>`, 12 points): the
chunk was gone on all three games 3 s later; no meat over 0.3 size anywhere (`coop.pool meat`). Lead not confirmed:
the host destroys a partner's eaten cell "with the loot the game asked for on the guest" (MSG_CELL_EAT arg & 1), which
drops new loot on the host only. Diagnostics added (cellsync.cpp): "cell: player N ate cX (...) with loot: here <the
loot cells and sizes>" on the host, and "cell: oversized meat cX size S (median M ...)" (WRN, once per cell) on every
machine every 5 s. A `/bug` right after seeing it carries both.
