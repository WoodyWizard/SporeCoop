---
id: BUG-9237
title: Network: an unchanged world-object snapshot is not sent again (at most every 3 s)
status: fixed
kind: feature
stage: all
reported: 2026-11-15
source: owner 2026-11-15 (4P A, traffic)
role: all
versions: 0.7.14.1-lineages60-dev
fixed_in: 0.7.14.1-lineages61-dev
branch: feat/lineages-47
commits: 809bb049
tests: manual Windows QA: the MSI two games, the net traffic line (A/B by the switch)
signature:
docs:
related:
---

## Report
Owner 2026-11-15 (4P A): world objects were the biggest item left of a guest's traffic after BUG-9235 (210 KB a minute).

## Root cause
Observed in the code (worldobj.cpp host_tick): each kind's snapshot of the objects near a guest went every 0.2-1 s with the same list nearly always.

## Fix
- A snapshot whose object list equals the last one sent to that guest goes again only after 3 s; a changed list goes at once. Switch wobj.same_snapshot.

## Verification
FAIL (off, the MSI, two games): MSG_WOBJ 213 KB, 330 messages a minute.
PASS (on): 118-128 KB, 177-191 messages a minute; the guest's fruit and trees bound and applied as before.
no-fail-run: Windows QA by hand.

## History
- 2026-11-15: opened.
- 2026-11-15: done (4P A).
- 2026-11-16: released in 0.7.14.1-lineages61-dev.
