---
id: BUG-9150
title: tribe: a late joiner's village starts with a stand-in of the host's creature and takes theirs in place
status: fixed
kind: feature
stage: Tribe
reported: 2026-11-12
source: owner decision 2026-11-12 (1B)
role: all
versions: 0.7.8-lineages54-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 7a701b8c
tests: tests/tribe_late_tests.cpp, tests/win_tests.cpp (stand-in list); tools/qa_win (manual two-PC run 2026-11-12)
signature:
docs: docs/re/tribe.md#tribe-stand-in
related: BUG-9135 BUG-9137 BUG-9138
switch: tribe.stand_in
---

## Report
Owner decision 14 ("1B"): a late joiner without a creature of their own gets their village at once with the host's creature as a stand-in, and it switches to their own creature when they choose or make one. Before, the village waited for their creature.

## Root cause
A request. Research 2026-11-12: the founder repair of BUG-9137 already puts a partner's own creature on a village in place (Tribe_SetSpeciesProfile on the 14 slots, cCreatureBase::SetSpeciesProfile on every member, the outfit history, a checkpoint reload); the host's own key cannot be the stand-in (every rule wants one species per seat).

## Fix
creations::make_stand_in: the host's creature records (model and metadata) under shared_instance(player, content), archived in the planet like a partner's creature (founder_ package, .duoguest-<key>). founders::read_stand_ins / add_stand_in: the planet's stand-in keys (<planet>.duostandins). tribe_late::founding_wait: NoCreature, HostCreation and FounderSpecies become StandIn (founded now); found_partners founds with the stand-in and tells everybody; wrong_founder_tick also repairs a stand-in seat. Guest: tribe::late_prompt 3 / 4 while the own village wears its stand-in ("Your village wears the host's creature for now. Choose your own creature below."). `coop.fix tribe.stand_in off`: the wait of before.

## Verification
no-fail-run: a new path (off: the old wait, still in the unit tests). QA 2026-11-12, fresh copy of the owner's Jiff save, host + Fourth (TITAN) + the MSI + a new profile without creations ("Newbie", TITAN): "stand-in 7CD8FBD8 for player 3: a copy of the host's 291E99C5", its village founded 77 m away about 15 s after it entered the world; its Co-op window showed the stand-in prompt; after a restart with creations it rejoined its seat by identity, chose one (prompt 4) and the host logged "founder repair (BUG-9137, BUG-9150 stand-in): the guest village was founded with 7CD8FBD8, now 7C2DD6C0 (seat 3)", "Newbie's village now wears their own creature."; after the checkpoint: the village's 3 members wear 7C2DD6C0, prompt 0, 0 faults on all four games.

## History
- 2026-11-12: opened.
