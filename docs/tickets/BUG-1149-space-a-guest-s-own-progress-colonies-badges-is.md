---
id: BUG-1149
title: Space: a guest's own progress (colonies, badges) is lost between sessions: the host keeps it in memory only
status: open
kind: bug
stage: Space
reported: 2026-11-15
source: discord
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Discord (bug question, 2026-11-15): when two players play Space over several sessions, what the guest did while the
host hosted is more or less lost each time they reopen the game.

## Root cause
Observed in code and docs: a guest always loads the host's share save at join; its own colonies and badges are kept
by the host in memory only (space_kept.h KeptStore, space_map.cpp, space_badges.cpp; docs/re/space.md
#space-kept-key), for a rejoin within the running host session. Nothing of the guest's own Space state is written
into the host's save (the allies mirror is undone before every save) and the guest keeps only its cargo hold on its
own disk (BUG-0679). When the host quits, the kept state is gone. Ship upgrades and claimed planets: not checked.

## Fix
To do: a design decision (owner): keep a guest's Space territory between sessions, either in the host's save (real
empire data for each player, the 0.8 direction) or as a per-world record on each guest's disk restored at join like
the hold (BUG-0679).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-15: opened.
