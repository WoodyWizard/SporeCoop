---
id: BUG-9089
title: Adventures only share a single move-to objective and partner markers
status: open
kind: bug
stage: Adventure
reported: 2026-11-07
source: User-approved full Adventure implementation
role: host guest
versions: 0.7.3.6-lineages25-dev
fixed_in:
branch: feat/lineages-adventures
commits:
tests: tools/tests/duo_adventure.py
signature: single_gate excludes talk, combat, parallel goals and terminal rewards
docs: docs/re/space.md#space-duo-adventure
related: BUG-9003 BUG-9004
---

## Scope and accepted decisions

The user has reopened full shared Adventures after the playable-through-Space
pass. Each founder keeps their own captain. The speaking player chooses when
dialogue changes the mission; the host commits that shared choice. Support up
to four participants in the Adventure design, independent of the unfinished
four-founder campaign bindings. Use targeted scenarios, not full-stage replays.

## Confirmed baseline limitations

adventure.cpp only credits a single move-to goal required once. coop.cpp only
ticks it with exactly two players. The Adventure adapter deliberately has no
native partner spawn function after BUG-9003; partners are markers. Native
NPC replication excludes Scenario mode. Dialogue, combat, parallel objectives,
mission results and captain XP are currently independent simulations.

## Implementation order and completion checks

1. Reproduce the first talk-goal divergence and inspect native goal/actor identity.
2. Establish host-authoritative run/act identity and idempotent shared progress.
   Reject stale runs, duplicate credits and mismatched mission content.
3. Bind safe native captain representations and scenario actors. Synchronize
   combat and object interactions without duplicating collectible/kill credit.
4. Synchronize speaking-player dialogue and all objective types, including
   parallel goals, defend/failure conditions and timed acts.
5. Qualify native success/failure, each captain's eligible reward exactly once,
   disconnect/rejoin, and return to the same Space campaign and personal ship.
6. Package only after native peer checks, clean exits and focused regressions.

## Current verification

Baseline FAIL: build/local/adventure-talk-baseline-fail.log records host act 2
and guest act 1 after the native mayor conversation. Required-once Talk goals
now use a host journal. PASS: adventure-talk-patch-a.log (host speaker),
adventure-talk-patch-b.log (guest speaker), and adventure-talk-reconnect.log
(guest offline during final parallel goal, catches up into act 3). All native
summaries/goals agreed, idle players stayed still, and fault counters were zero.
The first gate also passed on lineages25 before changing code.

The journal routes to all four session IDs, but these live runs used two games.
Packet truncation/bounds tests and the new DLL build pass. Native Replay followed
by the first gate passed on the new journal (adventure-new-journal-gate.log).
Opt-in captain bodies survived two same-act native resets per peer, but have
incorrect visual scale and remain disabled by default. Their native captain
export is fixed. Combat, actor replication, all other objective types and
terminal rewards remain open. Both games shut down natively with zero faults
and exit code 0. Marker-mode player-object sync mismatches remain. This ticket
is not fixed and full Adventures are not complete.

## Two-instance playthrough on 43-dev (2026-11-10, test/lineages-adventures)

Windows QA copy, host DuoQAAdvHost (instance 9) + guest DuoQAAdvGuest (instance 8), UDP 47839,
hosted at the galaxy menu, both opening the same adventure from Galactic Adventures.

Adventure Town (tutorial), played to the end, alternating captains per goal:
- act 0 move-to (host arrived), act 1 talk (host), act 2 three parallel talks (guest, guest, host),
  act 3 two talks (guest, host), act 4 talk (guest): every credit reached the other game once, the
  idle captain never moved, act/goals matched after every step.
- both games reached play state 5 together: "Mission Complete! 100%" on each, each captain with its
  own reward choice; Done returned both to the galaxy menu. Zero faults, no [ERR].
- `mod.reload coop` on both games + rehost/rejoin in act 3: goals still matched, play continued.

City Assault (combat: defeat 5 buildings, defend 5, defeat the city hall; goal types 3 and 11):
- no adventure credit was committed (the journal covers move-to and talk only).
- the allied army destroyed the same enemy buildings at the same moments in both games (each game's
  own simulation, identical while nobody intervened).
- the host's captain, sent alone into the enemy city, was killed (hp 0/300): the host's game failed
  the mission ("Mission Failed! 53%") while the guest's game completed it ("Mission Complete! 100%").
  Defeat goals (3) and defend goals (11) are separate per player; a player's defeat does not reach the
  partner. Zero faults on both.

Verdict for the player: story / exploration adventures (go to, talk to) are playable together;
combat adventures run as two parallel solo copies that can end differently. Not tested: collect and
timed goals, returning from an Adventure into a Space campaign, more than two players, two PCs.
Screenshots: out/qa43/adventure-town-both-complete.png, out/qa43/adventure-city-assault-split.png.

## Combat adventures shared (2026-11-10, feat/lineages-44)

Implementation (mods/coop/adventure.cpp, adventure_sync.h, creature_rescue.cpp):
- every objective kind goes through the host journal (credits keyed by goal and object, or by count);
  a defeat of an adventure object counts natively in each game (holding it failed the mission);
- deaths of adventure objects (buildings, creatures; id = marker + class) are mirrored both ways
  (Death messages, host relays), each object killed at most once, only while the mission plays;
- the host decides the end: its success / failure reaches every guest (Outcome); a guest never fails
  on its own;
- a captain is revived like a Creature-stage avatar (partner within 6 m for 1.5 s in 10 s), else gets
  back up beside a standing partner at 40%; a captain whose partners are all down dies and the host's
  failure ends it for everyone. Switches: adventure.combat, adventure.revive.
Tests: tests/adventure_sync_tests.cpp (counted credits, deaths, outcomes: PASS); QA (host 9 + guest 8):
- City Assault started together: host kill -> both 1/5 and identical buildings; guest kill -> both 2/5;
  alternating kills to the end: counts equal at every step, both "Mission Complete! 100%", 0 faults.
- guest downed beside the host: revived at 40% in ~2 s; guest downed 35 m away: back up 1.9 m from the
  host after 10 s; both downed: both games failed together.
- Adventure Town (regression): all 8 move/talk goals shared, both completed, 0 faults.
Bugs found on the way and fixed: false deaths during a load; credits dropped while the host sat in the
intro; held defeat credits failing the mission; the death list resent with every Hello.
Open: enemies' movement is each game's own (deaths, credits and the end are shared); missions started
apart in time catch up by mirrored deaths; two PCs, 3-4 players and adventures from a Space campaign
not tested yet.

## 44-dev (2026-11-10, feat/adventure-bodies)

Partner captain bodies are on by default (BUG-9113: the partner's scale, no spawn during the intro).
Host-simulated creatures (BUG-9114) are in the build, off by default: the host's creatures do not
strike a partner's body yet. Still open here: enemy movement per game, two PCs, 3-4 players.
