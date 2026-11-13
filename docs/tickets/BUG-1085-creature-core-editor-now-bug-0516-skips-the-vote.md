---
id: BUG-1085
title: Creature: core.editor_now (BUG-0516) skips the vote at the stage-end editor (Creature -> Tribe) and keeps it for the mating editor, the reverse of the design
status: fixed
kind: bug
stage: Creature
reported: 2026-11-13
source: Discord player report 2026-11-13 + forwarded analysis
role: all
versions: 0.7.11-0.7.13
fixed_in: unreleased
branch: fix/w49-tctl2
commits: 4d0dd0b1, 6d8d689a, 574d5d8d, 601e7ddf
tests: stage_end_vote, stage_end_guest, editor_now, creature_mating
signature:
docs: docs/re/creature.md#creature-editor-tags-runtime, docs/re/creature.md#creature-mating-call, docs/09-coop-design.md, docs/06-testing.md#testing-stage-end-pause-foreign
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

## Root cause, observed at runtime (2026-11-13, fix/w49-tctl2)
* The game's own mating: `editor request in Creature: editor 312E9D6A tag 012191CA` (lab 20261113-154950); the stage
  end the players' way (`coop.dna add 3000`, the brain caption, History and its check): `editor 465C50BA tag 064AA168`
  (20261113-155733, -154517). So `now_ok` was keyed on the stage-end editor.
* With the old keying (switch off): the host's stage-end editor opened at once ("vote none") and the guest's window
  showed "Evolve now" (20261113-155733); the host's mating waited for "Alice wants to evolve (Creature)" and the guest
  saw "I'm ready" (20261113-154950).
* The inferred guest path holds: the guest's game offers the stage-end button too (shared DNA: its own brain caption
  "your brain is fully grown ... evolve to the tribal stage"); its request reached its game, the guest went into its
  stage-end editor while the host played Creature, and after "done" into a Tribe of its own (Creature 3 s, Tribe 75 s
  later, the host still [Creature] in the co-op list; 20261113-154923; 20261113-154517 stopped in the editor).

## Fix
`core.editor_now_tag` (coop.cpp h_Submit): in Creature the mating request (tag 0x12191CA, not the landfall editor
0xFD4902BD, not within 300 s of a passed Cell -> Creature switch vote) is "an evolution inside the stage" for
`core.editor_now` (the host's editor at once, "Evolve now" offers); the stage-end request (0x64AA168) is a stage start:
the host's waits for the vote "<host> wants to move on to the Tribe stage"; a guest's in the host's Creature world is
refused (BUG-0920's editor-pending flag cleared, the stage stays drawn): its I'm ready during that vote, else the note
"the host moves everybody on to the Tribe stage (after a vote)". Every Creature request is logged with its editor and
tag. `coop.vote evolve` builds the mating tag (every test using it meant an in-stage evolution), `coop.vote stageend`
the stage-end tag; `coop.vote` prints `stage-end votes` and `guest stage-end requests refused`. Off: as 0.7.13.

## Verification
* stage_end_vote (host's stage end): FAIL with `core.editor_now_tag` off (20261113-155733: editor at once, "Evolve
  now"); PASS x2 (20261113-160059: the host stays in Creature with the vote, the guest's window "I'm ready", the host's
  editor after the guest's real click; key frame of the guest's window checked).
* stage_end_guest (the guest's press first): FAIL off (20261113-154517, -154923: the guest's editor, then Tribe alone);
  PASS x2 (20261113-155306: both games stay in Creature, the guest's stage drawn; key frames checked).
* editor_now (now on the game's own mating, not `coop.vote evolve`): FAIL off (20261113-154950: the vote); PASS x2
  (20261113-155537). creature_mating (updated: the editor at once and the offer) PASS x2 (20261113-155537).

* Regression (lanes 6 / 7): editor_now_off, vote_caption, own_editor_cancel, editor_request, evolve_in_place_creature,
  tribe_gameover_wait, play_tribe --quick PASS (20261113-160412); play_creature --quick, vote_caption PASS
  (20261113-161300); after the note delay tribe_gameover_wait, tribe_host_pause PASS (20261113-161954).

## History
- 2026-11-13: opened.
- 2026-11-13: fixed on fix/w49-tctl2 (`core.editor_now_tag`); the guest path confirmed and closed.
