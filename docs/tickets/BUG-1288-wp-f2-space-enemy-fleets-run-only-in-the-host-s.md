---
id: BUG-1288
title: WP-F2: Space enemy fleets run only in the host's system, two guests elsewhere see their own (fork BUG-9197)
status: open
kind: desync
stage: Space
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9197
role: guest
versions: 0.7.15.2
fixed_in:
branch: fix/w56-sync
commits:
tests:
signature:
docs: mods/coop/squadron.cpp, docs/re/space.md
related: BUG-0054, BUG-0677, BUG-1032
---

## Report
Found by the Lineages fork (PR #196), its BUG-9197 (row 7): with three or more players, two guests in a star system (or
on a planet) without the host each see their own native NPC ships (other ships, other fights). Assessed for WP-F2 of
0.7.16 (fix/w56-sync): present in ours, effort L, so not fixed in this package (the task's rule: fix only S/M).

## Root cause
Observed (code): squadron.cpp runs the NPC fleet relay on the host only: host_tick (squadron.cpp:562) lists the host's
ships and sends MSG_UFO_STATE to every guest; a guest applies the mirrors only while `C.with_host()` (the host's latest
state is in the same mode and world: puppets.cpp same_place) (squadron.cpp:912 the state apply, :1060 guest_tick drops
every mirror when the host is elsewhere, :1114 guest_fire). Two guests in another system both have their own game's
natives, unsynchronised, and their hits on each other's ships (MSG_UFO_HIT) go to the host, which does not have them.

## Design (not done)
A leader per place: the host where it is, else the lowest seat of the players in that place (place = mode + world id,
the same_place test, per player from its latest PlayerState). The leader runs the place's NPC ships; the other players
there mirror them.
1. Wire, without breaking a 0.7.15.2 peer: a new message type (id 74 of this batch's reservation, family space, minor
   bump) MSG_UFO_LEAD {u8 leader, u32 world, u32 mode} + the MSG_UFO_STATE body, sent by a guest leader to the host and
   forwarded by the host to the players of that place who announce space >= the new minor. The host's own
   MSG_UFO_STATE stays as it is (old guests keep working where the host is). Hits: MSG_UFO_HIT / MSG_UFO_DAMAGE of a
   guest-led place get the same wrapper (a leader byte in front) under the new type's kinds, routed through the host.
2. Leader hand-over: the next leader keeps the ships it mirrors as its own (the mirror objects stop being pinned and get
   their AI back; ids kept), or drops them and lets its game's natives stand (simpler; a visible swap). The fork kept
   them (its commit 59ca2bbc, about 360 lines).
3. Guest side: `with_host` becomes "my place has a leader that is not me" (mirroring) / "I lead" (send); h_TakeDamage's
   host checks (squadron.cpp:299, :324) become "I lead".
4. Tests: three instances in Space (host in system A, two guests in system B): the two guests' native ship ids and
   positions match within 2 s (`coop.fleet list`), a guest's shot on a mirror damages the leader's ship; twin with
   `coop.fix fleet.leader off` FAILs. Needs a three-instance Space fixture with two guests in another system (the lab
   has none ready).
Effort: L (protocol type + forwarding, hand-over, hit routing, 3-instance fixture). Impact: 3-4 player sessions only.

## Fix
Not done in WP-F2 (assessment and design only; the orchestrator decides the release).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-19: opened.
- 2026-11-19: assessed (L): design above, not fixed in WP-F2.
