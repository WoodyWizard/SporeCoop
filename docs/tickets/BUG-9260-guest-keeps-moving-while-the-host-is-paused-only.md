---
id: BUG-9260
title: Guest keeps moving while the host is paused (only the Paused by host cover)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-19
source: owner play test 2026-11-18
role: guest
versions: 0.7.14.1-lineages65-dev
fixed_in: 0.7.14.1-lineages66-dev
branch: feat/lineages-47
commits: c3ba2fbe
tests: tools/qa_win/qa.py (manual: MSI pair, coop.pauses add 4BF38A7 on the host)
signature: "Paused by host" cover up while the guest still moves
docs:
related: BUG-9227
---

## Report
Owner 2026-11-18: "when Host was paused in Live, the connected player was able to move still, even though 'host paused'
screen was up."

## Root cause
Observed in the code: BUG-9227 only drew the "Paused by host" cover over the guest's game while the host's state carried
PF_PAUSED; the guest's own game time kept running, so its creature moved and the world went on under the cover.

## Fix
coop.cpp host_pause_tick: while the host's latest state (under 3 s old, same stage and world) says paused, the guest
pauses its own game time with a reason of its own (world::time_pause 0x4BF38A7) and resumes it when the host does, the
state goes stale or the session ends. Switch: `coop.fix core.follow_host_pause off` (the cover only).

## Verification
no-fail-run: two PCs. Manual 2026-11-18 and 2026-11-19 on the MSI pair: the host paused (its History screen, and
`coop.pauses add 4BF38A7`): the guest's `coop.pauses` showed reason 04BF38A7 and the cover; after the host resumed the
guest showed no pause reason. Note: the host's death caption pauses its game too, so the guest waits with it.

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on feat/lineages-47 (c3ba2fbe).
