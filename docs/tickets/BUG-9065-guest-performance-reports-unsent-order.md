---
id: BUG-9065
title: Guest reports a performance request when no order was sent
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-06
source: Vantiegan consecutive performance trace
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance_selection.py
signature: empty selection reports Performance requested with zero packets sent
docs: docs/re/tribe.md
related: BUG-9064
---

On the fourth rival, the guest command returned success. The host retained
the previous rival's order and the guest's last outbound command was an old
instrument input. After the timeout all twelve guest members were deselected.
The target helper returns success even when send_cmd refuses an empty selection
or holds the command for world reconciliation. A failed transport enqueue also
increments its sent counter. See trace9064-B-play.log and trace9064-B-state.log.
The earlier second-rival timeout recorded an actual host order and remains a
separate investigation.

The direct target helper and the in-game Perform click now report an empty
selection, a world-sync hold or failed network enqueue. Sent counters advance
only after enqueue succeeds. Negative control: trace9065-negative.log, zero
packets with a success message. Fixed: trace9065-positive.log, zero packets
with an instruction to select villagers. A selected band then earns two more
ordinary alliances using real guest keys (trace9064-C-play.log). The test
selects the current band immediately before its order, including after a
checkpoint replaces the native actors. Windows unit suite passed.
