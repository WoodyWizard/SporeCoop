---
id: BUG-0465
title: the kraken (water monster) is not visible to other players
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: host
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs: docs/re/creature.md#creature-sea-monster
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

## Fix
Not fixed (decision, fix/w41-infra 2026-11-04). Observed (Ghidra): the sea monster is cSeaMonsterEvent, kind 6 of
the Drama Manager (docs/re/creature.md#creature-drama-events), started by each game for its own avatar. Decision:
it stays on a guest (the `creature.guest_events` mask leaves kind 6 out): inferred, it is the deep-water death
that keeps a swimmer near the island, and refusing it would let the guest swim off the map. Showing a partner's
sea monster needs a relay of the event (or death cause 9) to a cinematic / effect at the puppet: not analysed
(how the monster is drawn is unknown).

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: analysed on fix/w40-creature-acts (strings only), not fixed.
- 2026-11-04: fix/w41-infra: the event mapped (Drama Manager kind 6); kept per game by decision, not fixed.
