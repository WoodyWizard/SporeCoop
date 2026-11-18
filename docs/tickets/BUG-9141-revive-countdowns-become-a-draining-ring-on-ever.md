---
id: BUG-9141
title: revive countdowns become a draining ring on every stage with revives
status: fixed
kind: feature
stage: Cell Creature Tribe
reported: 2026-11-12
source: owner request 2026-11-12
role: all
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: c1ec1834
tests: tests/ring_timer_tests.cpp (geometry, colours); QA 2026-11-12 four games (Creature)
signature:
docs: mods/coop/ring_timer_logic.h
related: BUG-9127 BUG-9118 BUG-9146
switch: ui.revive_ring
---

## Report
Owner: "Instead of a 'revive' and countdown, can we do the usual circle with a draining ring that resembles time running out? Ensure this is on every stage that needs revives." Before: Cell "KO 23" over the cell (blinking) and square blocks for the revive; Creature / Adventure "Downed: 23 s for your partner to revive you (hold G to give up)" under the top bar; Tribe "[REVIVE 23s]" after the downed villager's name tag.

## Root cause
A request, not a bug. The stages with a revive: Cell (cellsync KO), Creature and Adventure (creature_rescue), Tribe (the last villager and the village down, tribe.cpp founder rescues). Civ and Space have none.

## Fix
ring_timer (ring_timer_logic.h pure, ring_timer.cpp D3D): a see-through dark disc, a dim track, the time left as an arc that drains clockwise from 12 o'clock (amber, red in the last half, blinking in the last quarter), a green pie inside while a partner revives, a white "+". Cell: over the downed cell (the hold as the green pie) and a small one over the off-screen arrow. Creature / Adventure: the downed player's own large ring under the top bar with "Hold G to give up" under it; a downed partner's ring over their arrow (its time from when this game first saw it down; the green pie while this player stands within reach). Tribe: over the downed villager's tag (the host also shows the revive pie). Switch `ui.revive_ring` (off: the text countdowns).

## Verification
no-fail-run: a new look (the text countdowns stay behind the switch). QA 2026-11-12, TITAN host + 2 TITAN guests + the MSI, Creature (Vandh test copy): `coop.giveup down` on the MSI: its own ring and "Hold G to give up"; the host saw the small ring over the MSI's red off-screen arrow. Cell (Cellfive test copy, host + the MSI): `coop.pool selfhit 20` on the MSI: its own ring over its cell, the host's small ring over the MSI's off-screen arrow. Not seen live: Tribe (the test knockout did not happen, BUG-9147).

## History
- 2026-11-12: opened.
