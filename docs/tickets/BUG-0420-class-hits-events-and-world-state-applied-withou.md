---
id: BUG-0420
title: class: hits, events and world state applied without a relation / partner / world filter (umbrella)
status: fixed
kind: bug
stage: All
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: unreleased
branch: fix/w46-k2a-state
commits: 6b49ee3d 8a54b2df
tests: world_gate_vote, civ_siege_npc, space_friendly, space_own_save; validate_tests (message classes)
signature:
docs: docs/07-modes.md#modes-message-classes
related: BUG-0701, BUG-0702, BUG-0703, BUG-0704, BUG-0705, BUG-0709, BUG-0648
---

## Report
Fixed per case: BUG-0374 (Creature pack), BUG-0131, BUG-0146, BUG-0231 (hand allow-list).
Siblings not covered: Space MSG_UFO_HIT applied on any NPC ship (squadron.cpp:587-601) and NPC hits on puppets forwarded for any attacker incl. host escorts (squadron.cpp:154-167) (obs); Civ joint siege counts NPC nations (0xFE) as a player, captures 1.5x faster (civ.cpp:662) (obs); MSG_VOTE not world-gated: a guest in another world gets the host's evolution editor (coop.cpp:831-839) (obs); host's social table enforced on a guest's own world every 0.5 s (social.cpp:591) (obs); shared Space money into a guest's own Space save (Space not gated in host_world.h) (inf); input validation and world gate are hand lists, new messages open by default (BUG-0201).
General fix: relation_allows(attacker, victim) in every hit path; is_playing_partner(id, in_my_world); world-state messages default-deny at dispatch, tagged where defined. Outcome check: monitor posse/ally rule in every stage; unit test every MSG_ classified.
Source: docs/generalisation-plan.md G3.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2a audit (0.7.11) of the sibling list against rel/0.7.11: every item was still present; the world gate was a hand list and hits had no relation check in Civ and Space.

## Fix
Shared methods: one class per message type with a build error on an unclassified one and default deny of unknown types while elsewhere (host_world.h msg_class, core.msg_classes); the world gate asked in the handlers that are session-wide (the vote) and in the guest's own-world loops (the species table); Space joins the gate for the shared money by the save name (host_world space_other). Children: BUG-0701 fixed (classes), BUG-0702 fixed (vote), BUG-0703 fixed (species table), BUG-0704 fixed (joint siege of an NPC army), BUG-0705 wontfix (Space friendly hits: the game ignores them; filter kept), BUG-0709 fixed (Space money into a guest's own Space save). BUG-0648 (not reproduced, its own package) is linked only.

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
The children's twin runs (their tickets). Detector over the K2a final runs on the merged branch: detect_* of all five stages out/lab/20261107-231824, play_space / play_civ / play_cell / play_tribe / play_creature out/lab/20261107-232019, detect_creature + play_creature out/lab/20261107-235443, detect_creature out/lab/20261108-001833: no signature of this class (none mapped to BUG-0420 in tools/detector_report.py; no new state or world rows from these mechanisms).

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): stun / hit rules now follow the creature they belong to (MSG_PLAYER_RULE, creature.stun_relay, creature.air_immune); the rest of the umbrella (Space, Civ siege, vote gate) not touched.
- 2026-11-08 (fix/w46-k2a-state, K2a of 0.7.11): children BUG-0701..0705, BUG-0709 closed; umbrella closed.
