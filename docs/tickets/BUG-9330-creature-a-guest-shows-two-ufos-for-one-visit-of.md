---
id: BUG-9330
title: Creature: a guest shows two UFOs for one visit of the host (its own relayed event's ship and the mirror of the host's)
status: open
kind: desync
stage: Creature
reported: 2026-11-21
source: found on fix/l47-ship-purge (BUG-9328 tests)
role: guest
versions: 0.7.14.1-lineages57-dev
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-ufo-ship-model
related: BUG-9328 BUG-9131 BUG-0558
---

## Report
Found while testing BUG-9328 (TITAN QA, host DuoQACreatureHost Samuth, guest DuoQACreatureGuest): after the host's
`coop.drama start 2`, `coop.drama ship` on the guest listed two live UFOs, the host's game one.

## Root cause
Observed (code and runtime): since BUG-9131 the host relays the UFO visit and the guest's game starts the same event
itself (a ship over the guest's own avatar), and creature.ufo_mirror (BUG-0558) also shows a mirror of the host's ship at
the host's ship's pose. Both are made by the event's spawn function. When both players stand together the two ships
overlap; apart, the guest sees one over itself and one over the host.
Not fixed (not part of the BUG-9328 task): one of the two is redundant (the relayed event's ship takes nothing on a guest,
player_rules pick_epic). With BUG-9328 both now show the same ship model.

## Fix
Not done.

## Verification
To reproduce: host and guest in Creature, host `coop.drama start 2`, guest `coop.drama ship`: two `ufo` lines.

## History
- 2026-11-21: opened.
