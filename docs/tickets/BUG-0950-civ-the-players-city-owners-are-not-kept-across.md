---
id: BUG-0950
title: Civ: the players' city owners are not kept across a host's load: every city is dealt out anew
status: open
kind: bug
stage: Civ
reported: 2026-11-12
source: fix/w48-captures 2026-11-12 (code reading)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/civ.md#civ-capture-keeps
related: BUG-0875, BUG-0775
---

## Report
Found while fixing BUG-0875: the mod's owner table of the players' cities (g_city_owner, g_city_gifted,
g_city_captured) lives only in the host's memory. A host that loads its Civ world again (the same save after a menu,
a crash rejoin, the next session) deals every city out anew in turns: a city a guest captured, bought or was given
may become another player's.

## Root cause
Inferred from the code (confidence high): civ.cpp on_world_change and on_session_end clear g_city_owner and the
co-op tables; host_assign_cities then gives the capital to the host (or shares it) and the others in turns. Nothing
is saved. Not run.

## Fix
Idea: keep the owners by city position (and the host's save name) in the host's session state or a small file next
to the save, and restore them after the load.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-12: opened.
