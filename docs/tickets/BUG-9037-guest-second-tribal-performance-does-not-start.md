---
id: BUG-9037
title: Guest second Tribal performance does not start after a friendly win
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: Two-performance alliance qualification
role: guest
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance.py --repeat
signature: first native win relation3 then no second performance snapshot
docs: docs/re/tribe.md
related: BUG-9035
---

m2-alliance-b.log: the first performance reaches native progress1.0 and
NPC-to-guest relationship3 on both peers. A second action26 order is accepted
by the host but does not produce a live performance snapshot within45 seconds.
Both games exit with zero native faults. This blocks alliance qualification.

Distinguish native end-of-performance cleanup, actor selection and native
social eligibility before changing the game integration. The shared alliance
totem assertion has not been reached. No campaign reward is proved yet.

Ticket created directly using the existing schema because tools/tickets.py new
imports Unix-only fcntl and cannot run under the Windows Python environment.

Root cause: the completed performer remains in terminal node9 of B46E8803,
with the old action26/session6. Reissuing the same action does not clear it.
An ordinary walk between performances permits a second win (completion-a).
Native CancelAction DA61D0 cancels/replans, but a same-frame action26 reinstates
the old action before it exits (FAIL repeat-a). The fix cancels completed own
performers, then revalidates and submits the requested performance after150ms.
New commands supersede the queued request; world cleanup discards it. No raw
object pointers are retained and active performances are not cancelled.

PASS m2-performance-repeat-b.log: two consecutive native wins, relationships
0 to3 to7 on both peers, original species retained, zero native faults. No
movement workaround is used. tribe.performance_repeat preserves the old path
for regression checks. Missing campaign credit is tracked separately in9038.
