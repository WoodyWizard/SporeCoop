---
id: BUG-0559
title: Creature: parts of a pack the guest lacks (Creepy & Cute) are drawn as big blocks, and the host's colours are another shade on the guest
status: open
kind: bug
stage: Creature
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: both
versions: 0.7.5
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0424, BUG-0452
---

## Report
The host: the guest sees some parts of the host's creature as big blocks (different from part to part); the guest does not have the Creepy & Cute pack. On Discord: the host's colour is a completely different shade on the guest's screen. Before 0.7.4 this crashed; now a fallback part is drawn, it should be a similar part.
Reports: SporeCoop-report-2026-11-04 184642 (out/reports/2026-11-04, not in git).
Screenshots (the reports' shots/, guest and host at the same session second): s4687: only the host's screen (its creature looks right there); no guest screenshot of the blocks.
Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* Observed (guest log 17:28:33): "Creepy & Cute parts pack NOT present", then "the host's own creation
  29116124: part A115CFC8 unknown here" (and 82A9416E): the guest draws the fallback part of BUG-0452
  (creations.unknown_parts) instead of a similar part. Colours: not in the logs.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
Not run again: unknown_parts (BUG-0452) covers the no-crash part only. Package C1.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open (C1), observed in the guest log.
