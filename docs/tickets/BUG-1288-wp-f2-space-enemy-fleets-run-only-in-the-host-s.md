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
branch: fix/w73-fleet
commits: 17f9fcaa
tests: tools/tests/coop_space_fleet_leader.py, tools/tests/coop_space_fleet_host_leaves.py, tests/space_tests.cpp (test_fleet_leads), tests/wire_tests.cpp (test_space_ufo), tests/fuzz_decoders.cpp
signature:
docs: mods/coop/squadron.cpp, mods/coop/squadron_logic.h, mods/coop/wire/space_ufo.h, docs/re/space.md#space-guest-leaders, docs/04-netcode.md#netcode-ufo-lead
related: BUG-0054, BUG-0522, BUG-0677, BUG-1032, BUG-1282
switch: fleet.leader
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

## Design (the WP-F2 assessment; the Fix below differs: sticky leaders named by the host, no promotion)
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
WP-S1 of 0.7.17 (docs/plan-0.7.17.md item 6, Space part), branch fix/w73-fleet. The starting point was the outside
branch fix/agy-open-bugs (commit 3b126046, never built or lab-run there; its review out/research/agy-review-space.md:
"merge after fixes"). Its wire part (a host-relayed message with three kinds, the checks, the fuzzed decoders) is ported;
its game side is redone per the review:
* **The host names the leaders** (squadron_logic.h `step_leads`, MSG_UFO_LEAD K_LEADS, reliable, on a change and every
  2 s while there are rows). A place counts when it is a valid Space place (mode Space, world != 0), the host is not in
  it and a capable guest (space 1.3 or newer, `peer_families`) is. Its leader **stays** while it is there (a later
  arrival never takes the lead: its game's natives are fresh, the leader's are what the place was fighting), and for
  `kLeaderGrace` 6 s while it is in no place (a zoom to the galaxy view, an editor, a moment without a state); when it
  left the session or went to another place: the lowest id of the capable guests there. A row is announced only with
  somebody to mirror the leader (two players, or a follower while the leader is in its grace), so **a place with one
  guest plays exactly as 0.7.16.1** (2 players never get a guest leader). The guests only read the table: in a mixed
  session they cannot disagree (the review's mixed-election row); an older guest is never named nor sent anything.
* **No promotion of mirrors**: a follower that becomes the leader drops its mirrors like every other follower (the ships
  pop once; the departing leader's escort, type 3, is never kept as anything). The host-led transitions are exactly
  0.7.16.1's (squadron_logic.h `step_role`: Mirror(host) -> None is left to guest_tick's drop as before, the host's ids
  are never cleared, the chunk receiver is never reset); only a guest-led side drops, clears or resets.
* **Editor frames and the session end as in 0.7.16.1, whatever the switch**: tick() returns outside Space as before
  (mirrors and ids stay across an editor, the role changes when the game is back); on_session_end destroys every mirror
  (also a guest leader's followers' ones) wherever the game is, then forgets the roles and the table. MSG_UFO_DAMAGE
  from the host is applied whenever the guest is in Space, as before (the review's nit).
* **Rivals: no guest leaders** (the host's table is empty while `spaceext::rivals()`): a War between guests is not
  modelled on this path; Rivals is 0.8's.
* The leader plays the host's part of 0.7.16.1 there: leader_tick / leader_send_shots (the former host_tick /
  host_send_shots; a guest leader's MSG_UFO_STATE body goes in K_STATE), its NPC ships' hits on a follower's puppet in
  K_DAMAGE, a follower's hit on a mirror in K_HIT (the host writes the shooter's id); apply_hit / apply_damage are
  straight extractions of the 0.7.16.1 handlers. The host checks every K_* against its table (an active row's leader,
  a capable follower in that place, the victim there) and relays from wherever it is (an editor, the galaxy view).
* Wire: MSG 78 UFO_LEAD (the WP-0 reservation, the agy branch's 74), family space 1.3 (bumped in WP-0, re-locked here
  with wire/space_ufo.h); `wire::space_ufo::kMinor` is the gate's constant.
* `coop.fix fleet.leader off`: no table, nothing relayed, a guest away from the host plays its own ships (0.7.16.1).
* Status: `coop.fleet list` prints `space leader: fix= role= leader= place= leads= ...` and one `space lead <world>
  <leader>` line per announced row (the host's table, a guest's copy).
* Credit: the idea and the first version of the relay came from the outside agent's branch fix/agy-open-bugs (no
  IltonBJSilva PR for this item).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->
Open, waiting for the lab run (no lab by day, out/RULES-0.7.17.md). pending night run: space_fleet_leader (PASS
expected), space_fleet_leader_off (`SPORE_FIX_OFF=fleet.leader`, must FAIL at [leader] and [ships]),
space_fleet_host_leaves (PASS; a regression guard for the agy branch's promotion, 0.7.16.1 passes it too). Unit tests
pass (make test, 2026-11-23): space_tests test_fleet_leads, wire_tests test_space_ufo, fuzz_decoders (ufo Lead*).
Old-peer run against 0.7.16.1 (space 1.2): part of the final check.

## History
- 2026-11-19: opened.
- 2026-11-19: assessed (L): design above, not fixed in WP-F2.
- 2026-11-20: implemented on fix/agy-open-bugs (3b126046, outside agent; review: merge after fixes).
- 2026-11-23: redone on fix/w73-fleet (WP-S1 of 0.7.17) per the review: the host names sticky leaders, no promotion,
  editor and session end as 0.7.16.1, 2-player sessions unchanged, MSG 78; lab tests written, waiting for the night run.
