---
id: BUG-1565
title: History: per-player starting evolutionary path / philosophy picker for worlds started past Cell
status: open
kind: feature
stage: All
reported: 2026-11-25
source: 0.8.1 Rivals isolation audit
role: both
versions: 0.8.0.2
fixed_in:
branch: w081/rivals-midgame-history
commits:
tests: rivals_midgame_history, rivals_midgame_history_off
signature:
docs: docs/11-rivals.md
related: BUG-1470, BUG-1557
---

## Report
When a host starts a new game directly at Creature, Tribe, Civilization, or Space stage (skipping earlier stages), `history::seed()` copies the host's prior-stage consequence cards to every guest on first sight. Guests have no way to choose their own starting evolutionary path or Space philosophy (unlike solo new-planet setup).

## Root cause
`mods/coop/history_logic.h` and `mods/coop/history.cpp` only seed a newcomer's record from the host's cards and accept live diet/Tribe/Civ updates; there is no guest-to-host prior-stage path selection request or UI.

## Fix
- Add a guest-to-host prior-stage path selection message (`wire/history.h`), portable validation (`history_logic.h`), host application (`history.cpp`, behind `COOP_FIX("history.midgame_pick", "BUG-1565")`), console command `coop.history pick ...`, and compact F9 UI controls allowing guests to choose their own prior-stage cards / target Space philosophy cleanly without affecting the host.

## Verification
Pending implementation and Wine lab ON/OFF twin verification.
