---
id: BUG-0418
title: class: player-species / avatar-only game rules a guest misses (umbrella)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content
commits: 8911e865 e896b4a8
tests: creature_grow, pack_defends_partner, flock_relay, hostile_party, creature_brain_hp
signature:
docs: docs/re/creature.md#creature-player-species-callers
related: BUG-0732, BUG-0733, BUG-0734, BUG-0735, BUG-0736, BUG-0737, BUG-0738, BUG-0375, BUG-0462, BUG-0463, BUG-0501, BUG-0554, BUG-0555, BUG-0639, BUG-0640, BUG-0641, BUG-0642, BUG-0645
---

## Report
Root: on a guest playing its own creation, cSpeciesManager+0xA4 (PlayerSpecies 0x4DF490, 60 callers; IsPlayerSpecies 0xC0C430 = creature vt 0xD0) stays the host's species; on the host the guest's puppet is a stranger. Fixed per consumer only: BUG-0375 (BaseMaxHitPoints 0xC04170), BUG-0358 (ability bar 0xD1E830).
Siblings not covered:
- creature scale vt 0x80 = 0xC04B50: player species x brain factor table 0x150C8E4 (1.0..1.5) + AvatarNormalizingScale 0xD2E720: the guest's creature never grows with brain upgrades (obs); puppet scale set once at spawn (modes.cpp:65), PlayerState has no scale: partner never grows on the other screen.
- hostile-species targeting 0xD7D9F0 (relation 2/5 target player-species adults): puppets skipped.
- herd friendly test 0xC6ABF0, AI flags 0xC04440 (0x1000 player species, 0x200 brain > 4), 0xC02870 / 0xC0A100 strength relative to player species, 0xC22450 default species, 0xD43DE0 family spawn of PlayerSpecies, nest/home-nest UI 0xD40AF0, 0xB6CB10, 0xB6BBF0, 0xE8DF30, 0xD425A0, 0xC6D960, 0xC6E690.
- guest social win never posts 0x60B4123 (BUG-0179); rewards re-implemented instead of the game's functions (BUG-0191 cargo, BUG-0192 shooter modifiers); avatar-only post in CreatureAttackTarget 0xC02B40 outside the ai swap; guest sneak +0xB80/+0xBB1 never mirrored (hearing check 0xC0D6C0); no pack defence for a guest (npc.cpp:216-217, :249).
- Tribe/Civ/Space callers of 0x4DF490 (~40) not classified.
General fix: guest-side upstream rule (avatar species = player species for rule readers) after auditing callers; run the game's reward functions with the puppet as avatar. Outcome check: own_rule vs as_player_species (HP, scale, damage) and puppet scale = player's scale.
Source: docs/generalisation-plan.md G1.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
K2b audit (0.7.11) of the sibling list against rel/0.7.11: scale (own avatar and the partner's puppet), pack defence, the guest's own flock and the PlayerSpecies caller table were still open; hostile targeting, the AI flags and strength were checked at runtime / statically.

## Fix
Shared method: the game's own rule run with the guest's creature as the player species or the avatar (a PlayerSpecies override only while vt 0x80 of the own avatar runs; the posse attack command and the summon event's update run on the host with the puppet as the avatar), and one classification of the ~60 PlayerSpecies / IsPlayerSpecies callers (docs/re/creature.md#creature-player-species-callers). Children: BUG-0732 fixed (creature.own_scale), BUG-0733 fixed (creature.puppet_scale), BUG-0735 fixed (npc.pack_defends_partner), BUG-0736 fixed (creature.flock_relay), BUG-0738 fixed (the caller table), BUG-0734 not-reproduced (the AI swap already makes hostile animals go for the guest; test hostile_party kept as a guard), BUG-0737 wontfix (flag 0x1000 has one reader on an unknown stimulus, 0x200 none; strength and default species are the world's on every game). BUG-0191 / BUG-0192 are Space feature notes, not children.

## Verification
no-fail-run: an umbrella; each child names its own FAIL and PASS runs.
Detector over the K2b final run on the merged branch out/lab/20261108-054842 (detect_* of all five stages, play_* of all five stages, every K2b test, SPORE_DETECTOR=20; report out of tools/detector_report.py): no signature of this class; the rows seen map to other tickets (BUG-0475, 0476, 0477, 0478, 0479, 0480, 0487, 0497, 0539, 0579, 0617, 0677, the BUG-0421 egg / guest-local rows, and Civ planet village / industry rows of K8's BUG-0659).

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): covered in one place for the guest (player_rules.cpp + creature_hp.cpp + social.cpp): stun (BUG-0554), flight (BUG-0555), friendly pack (BUG-0556), nest ally flag (BUG-0561), Tribe ring (BUG-0570); the list of the player checks and their answers: docs/re/creature.md#creature-player-rules. Still open: scale, brain AI flags, the roar, the guest's own flock, Tribe / Civ / Space callers of PlayerSpecies.
- 2026-11-06: Discord report 2026-11-06 (0.7.8): children BUG-0641 (spit / charge interrupts), BUG-0642 (the own creature misses the player-species nest rules); related BUG-0639, BUG-0640.
- 2026-11-08 (fix/w46-k2b-content, K2b of 0.7.11): every sibling item a closed child or covered; umbrella closed.
- 2026-11-08: released in 0.7.11.
