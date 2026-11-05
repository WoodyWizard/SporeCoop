---
id: BUG-0418
title: class: player-species / avatar-only game rules a guest misses (umbrella)
status: open
kind: bug
stage: Creature
reported: 2026-11-01
source: generalisation audits 2026-11-01 (docs/generalisation-plan.md)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-01: opened.
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): covered in one place for the guest (player_rules.cpp + creature_hp.cpp + social.cpp): stun (BUG-0554), flight (BUG-0555), friendly pack (BUG-0556), nest ally flag (BUG-0561), Tribe ring (BUG-0570); the list of the player checks and their answers: docs/re/creature.md#creature-player-rules. Still open: scale, brain AI flags, the roar, the guest's own flock, Tribe / Civ / Space callers of PlayerSpecies.
