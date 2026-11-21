---
id: BUG-9286
title: Own progress: <planet>.spo.duoprogress not written warnings in a new game before its first save
status: open
kind: bug
stage: Cell
reported: 2026-11-20
source: owner 2026-11-20 (friends build test)
role: host
versions: 0.7.15.94
fixed_in:
branch: feat/own-progress
commits:
tests: own_progress_cell
signature:
docs: docs/01-architecture.md#architecture-own-progress
related: BUG-9265, BUG-9284, BUG-9287
---

## Report
The owner's log of the friends build 0.7.15.94 (host, a new Cell game): "WRN own progress: <planet>.spo.duoprogress not
written", twice in the first seconds of the game (the same pattern: "party: not saved: a saved planet and a valid party
are required", four times).

## Root cause
✔ (code + log) founders::write_side writes the side file only next to an existing <planet>.spo (founders.cpp
path_for). A new game has no .spo until its first save; the host's records were dirty from the first second and every
try (3 s apart) failed and warned. After the first save (here the world share 3 s later, 17:29:57) the next try wrote
the file: no data was lost, the warning was noise. Not the reason the progress "did not become individual": the friend
ran the official 0.7.15.5 (BUG-9284), whose game takes the host's food total.

## Fix
own_progress.cpp save_outside: when the planet's .spo does not exist yet the records stay dirty and wait without a
warning (one INFO line "waits for the planet's first save"; `waits_save=` in coop.progress). `coop.fix
core.own_progress_quiet off`: the warning of 0.7.15.94. (The party's warning, feat/lineages-party, has the same cause:
left as it is, ticketed here as related.)

## Verification
Built (WSL). In-game check (a new Cell game on lane A: no "not written" warning, "waits for the planet's first save"
once, the file next to the planet after the first save): see History.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on feat/own-progress.
- 2026-11-20: lane A, a new Cell game on 0.7.15.95: no "not written" warning, one "waits for the planet's first save", the file written after the first save (saves=2). (The party's "not saved" warnings of the same moment: fix/party-new-planet.)
