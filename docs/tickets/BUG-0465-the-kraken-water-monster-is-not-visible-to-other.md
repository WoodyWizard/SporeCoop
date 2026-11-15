---
id: BUG-0465
title: the kraken (water monster) is not visible to other players
status: fixed
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in: 0.7.9
branch: fix/w45-creature-reports
commits: 7d4481bd
tests: creature_sea_monster (coop_creature_sea_monster.py)
signature:
docs: docs/re/creature.md#creature-sea-monster, docs/re/creature.md#creature-sea-monster-runtime, docs/re/creature.md#creature-sea-monster-relay
switch: effects.world_xform
related:
---

## Report
Host's mark (s 188, 18:06): 'Kraken in water is not visible for other players.' The game's water monster (not an NPC herd creature?) is not mirrored to guests. Class G4 (BUG-0421).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not fixed (fix/w40-creature-acts, 2026-11-03). Observed (Ghidra strings): the sea monster is cSeaMonsterEvent, an
event of the avatar in deep water; the avatar's death of cause 9 plays CRG_AvatarDied_SeaMonster (0xD38E60).
Inferred, not seen at runtime: it is made by the drowning player's own game for its avatar only, so the partner's game
never has it. See docs/re/creature.md#creature-sea-monster.

**Observed at runtime (fix/w44-guest-player3, 2026-11-05)**: the host's avatar put over deep water (lab lane 5,
out/lab/20261105-140627): the sea monster event started after 4 s, the host's game played the death cinematic with the
monster (a crocodile-like head out of the water) and its avatar hatched at the nest 8-10 s later; the guest's game had
no event, object or effect for it, the host's puppet only died (PF_DEAD). The monster is part of the cinematic of the
dying player's game. The cinematic's world effects (six ids, each played alone: out/lab/20261105-140852) are not the
monster: inferred, it is a cinematic actor. docs/re/creature.md#creature-sea-monster-runtime.

**fix/w45-creature-reports (2026-11-06), observed**: the monster is the effect crg_seamonster_attack 0x8A71529C that
the cinematic script (Spore_Game.package 0x024A0E52 / 0x40890200 / 0x9EA3031A, state CRG_AvatarDied_SeaMonster0) starts
at the avatar. Since 0.7.4 the world effect relay (BUG-0451) sends it and the partner's game plays it at the place
(CORRECTION of the line above: the w44 run's guest log has it played; its camera was elsewhere). With
`creature.effects_relay off` (the 0.7.3.4 state of the report) the guest's screen shows nothing at the spot
(out/lab/20261106-031408). The relay sent it with the position only (identity rotation, flags 0): the guest's monster
stood 32 degrees off the planet's up and was drawn tilted, in pieces sideways (out/lab/20261106-031307).

## Fix
fix/w45-creature-reports: `effects.world_xform` (effects.cpp): a world effect is sent with the game's own transform
(effect +0x9C: flags, position, scale, 3x3 rotation), so the partner's copy has the owner's orientation and size; the
monster rises upright out of the water on the partner's screen. `coop.fix effects.world_xform off`: position only (0.7.8).
Earlier notes:
Not fixed (decision, fix/w41-infra 2026-11-04). Observed (Ghidra): the sea monster is cSeaMonsterEvent, kind 6 of
the Drama Manager (docs/re/creature.md#creature-drama-events), started by each game for its own avatar. Decision:
it stays on a guest (the `creature.guest_events` mask leaves kind 6 out): inferred, it is the deep-water death
that keeps a swimmer near the island, and refusing it would let the guest swim off the map. Showing a partner's
sea monster needs a relay of the event (or death cause 9) to a cinematic / effect at the puppet: not analysed
(how the monster is drawn is unknown).

## Verification
creature_sea_monster (coop_creature_sea_monster.py): the host's creature into deep water; the guest's game must play
the monster effect (its effect object, active) within 1 m of the host's and within 3 m of the host's puppet as drawn
there, with the host's rotation (each entry within 0.05) and scale (5 %), upright (an axis within cos 0.95 of the
planet's up). The guest stands on the shore 65 m away and turns its camera to the spot (right-drags) for the shots.
- FAIL with `SPORE_FIX_OFF=effects.world_xform`: out/lab/20261106-030327 (rotation off by 1.387, up cos 0.851),
  20261106-030700, 20261106-031307 (shots: the monster tilted, in pieces).
- PASS: out/lab/20261106-030230, -030441, -030555, -031141 (shots: the monster rises upright out of the water on the
  guest's screen), 4 of 4 in a row out/lab/20261106-031612 (--repeat 4); FAIL off again out/lab/20261106-032138.
- `SPORE_FIX_OFF=creature.effects_relay` (the report's 0.7.3.4 state): no monster on the guest's screen, out/lab/20261106-031408.
- Screenshot montages (not in git): out/bug0465/ of the worktree.
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts (strings only), not fixed.
- 2026-11-04: fix/w41-infra: the event mapped (Drama Manager kind 6); kept per game by decision, not fixed.
- 2026-11-05 (fix/w44-guest-player2, P2 of 0.7.8): not started (the time went to BUG-0501, BUG-0558, BUG-0496, BUG-0557).
- 2026-11-05 (fix/w44-guest-player3, P2b of 0.7.8): reproduced at runtime (the monster is in the dying player's death cinematic only), not fixed: needs the cinematic's actor shown at the puppet.
- 2026-11-05 (fix/w45-creature-reports, P3 of 0.7.9): not fixed; the time went to BUG-0557 / BUG-0383, BUG-0558 and BUG-0458 (by player impact: the monster is the drowning player's own death cinematic, seen by the partner as the puppet dying in deep water). Next step unchanged: read CRG_AvatarDied_SeaMonster's actors (the cinematic's data) and show that model at the puppet without the camera.
- 2026-11-06 (fix/w45-creature-reports, follow-up of P3): the monster found (an effect of the death cinematic), shown upright on the partner (effects.world_xform); fixed.
- 2026-11-07: released in 0.7.9.
