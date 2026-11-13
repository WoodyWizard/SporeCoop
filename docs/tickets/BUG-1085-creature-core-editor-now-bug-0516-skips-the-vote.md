---
id: BUG-1085
title: Creature: core.editor_now (BUG-0516) skips the vote at the stage-end editor (Creature -> Tribe) and keeps it for the mating editor, the reverse of the design
status: open
kind: bug
stage: Creature
reported: 2026-11-13
source: Discord player report 2026-11-13 + forwarded analysis
role: all
versions: 0.7.11-0.7.13
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creations.md, docs/re/creature.md#creature-mating-call, docs/09-coop-design.md
related: BUG-0516, BUG-0273, BUG-0315, BUG-0177, BUG-0457
---

## Report
A Discord player (0.7.x, version unknown) says the guest cannot control its units in Tribe. The owner forwarded an
outside analysis; its first point is that the Creature -> Tribe change has no ready vote since core.editor_now
(BUG-0516). Checked on rel/0.7.14 (code, docs, recorded lab runs): that part holds, and the reverse holds too.

## Root cause
Observed in code and in recorded runs, confidence high:
* h_Submit (mods/coop/coop.cpp:2344-2346) lets the host's request through without a vote when `+0x90 == 0x64AA168`
  in Creature (`now_ok`), on the premise of BUG-0516 that this is "the mating call's request".
* That premise was corrected on 2026-11-08 (docs/re/creations.md, CORRECTION under "own editor items";
  docs/re/creature.md#creature-mating-call): 0x64AA168 is built by 0xD42E50 on message 0x4471C60 with +0xDE set,
  which only the stage-end evolve button (0x6455B55, brain level 4 / CRG_BrainUpgrade4, the Timeline UI) sets: it is
  the end-of-stage editor that leads to Tribe. The mating editor is 0xD3C650 with tag 0x12191CA.
* So `core.editor_now` acts on the wrong editor:
  * Creature -> Tribe: no vote. Lab fix/w47-final12d out/lab/20261111-212008 campaign_tribe (rel 0.7.11.2 games):
    host log line 376 "evolution: the host's editor opens at once, the others get an offer (core.editor_now)",
    editor 465C50BA, back in Creature (the stage cinematic, 78.9 s), line 483 "Alice entered the Tribe stage:
    everybody follows". The guest's log: "Alice evolves now: press Evolve now (F9) to evolve yours too", then
    "offer of editor 465C50BA ends: time is up" after the host is already in Tribe. The guests were not asked; they
    were offered an in-stage evolution of their own creature that makes no sense at the stage end.
  * Mating evolutions still wait for the vote: fix/w48-creature out/lab/20261112-083019 long_creature host log
    lines 863, 982, 1212, 1430: "vote: start 'Alice wants to evolve (Creature)'" (editors 465C50BA, 312E9D6A).
* docs/09-coop-design.md 9.3 and BUG-0516's Fix say the opposite ("an evolution inside the Creature stage has no
  vote ... stage starts keep the vote"). The test editor_now (tools/tests/coop_editor_now.py) builds the request
  itself (`coop.vote evolve`, build_editor_request with tag 0x64AA168), so it passed on the mod's own request, never
  on the game's mating or stage-end editor.
* Inferred, not run: docs/09-coop-design.md 9.3 says "a guest cannot advance alone: their transition button leads
  to the vote". In h_Submit a guest's own request (any tag, creature.mode own) goes to the game (coop.cpp:2370-2373,
  the guest branch): a guest that presses the stage-end evolve button first would go through its editor into a Tribe
  of its own copy while the host is still in Creature. There host_world::classify gives NotGated (host_world.h:55,
  the host's state is another mode), host_out_of_stage() is 0 (the host is in a stage, coop.cpp:1178) and
  own_world::host_left() (BUG-0797) is read only by cellsync (cellsync.cpp:4410), so tribe.cpp local_play() is false;
  with no host Tribe states the guest's tables are empty: its members select (owner_of 0xFF) but h_WalkSelected /
  h_DoActionForObjects send an order with no ids and do not run the game's own (tribe.cpp:695-720, 818-840). The host's
  follow_phase pulls such a guest back into its Creature world after 5 s (coop.cpp:3083-3112), so this is a window of
  seconds plus a load, not a dead Tribe for good. Not seen in any recorded run; whether the guest's game even offers
  the stage-end button with the shared DNA is not known.

Not a cause of "the guest cannot control its units in Tribe" for an ordinary session: in the recorded campaigns the
guests follow the host into Tribe and play there.

## Fix idea
Key `now_ok` on the mating tag 0x12191CA (and the Creature stage, not the first Creature editor after Cell, which
BUG-0315's arrival vote owns), and treat 0x64AA168 in Creature as a stage start: the vote ("wants to move on to the
Tribe stage"), as BUG-0315 does for Cell -> Creature; a guest's own 0x64AA168 request becomes its "I'm ready" (or is
refused with a note) instead of reaching the game. Fix the editor_now test to use the game's own requests (the mating
recipe of docs/re/creature.md#creature-mating-call, the campaign's stage-end path).

## How to test
* editor_now on the game's mating editor (coop_creature_mating.py recipe on the host): today a vote starts (FAIL),
  with the fix the host's editor opens at once and the guest gets the offer.
* A new stage-end test (campaign advance creature -> tribe with two players, as coop_transition_net.py does): assert
  that the host's editor waits for a vote and that the guest's window shows the ready button, not "Evolve now"; today
  the host log has "core.editor_now" (FAIL).
* The guest's own stage-end press: same fixture, `AddEvolutionPoints 3000` on the guest, History and its check on the
  guest first; assert that the guest's game stays in Creature (today, inferred: it reaches Tribe alone).

## History
- 2026-11-13: opened.
