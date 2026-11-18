---
id: BUG-1243
title: Cell: when one player attacks, the other player's cell plays the attack animation too
status: fixed
kind: bug
stage: Cell
reported: 2026-11-18
source: owner: players' video
role: all
versions: <0.7.15
fixed_in: unreleased
branch: fix/w53-cellfx
commits: d4a043c1 0c2eeb16
tests: cell_partner_respawn, cell_far_fight, cell_attack_anim, cell_attack_electric, cell_attack_jaw, cell_attack_spike, cell_attack_mouth, cell_electric
signature:
docs: docs/archive/known-issues-2026.md#issue-partner-cell-attack-anim, docs/re/cell.md#cell-partner-attack-anim, docs/re/cell.md#cell-partner-hatch
related: BUG-1241, BUG-0715, BUG-0716, BUG-0238
---

## Report
A players' video (players on a version before 0.7.15), seen by the owner: in the Cell stage, when one player
attacks something, the other player's cell plays the attack animation too.

## Root cause
Not found. Measured with `coop.pool anims` (the cell animation +0x1B0 of the own cell and of every partner's puppet,
both games, every 0.05 s) and `coop.effects` (cell fixture, both cells the world's species):
* Each player in turn bites the nearest creature (the game's Bite, after swimming at it): no animation of the
  attacker reached the other player's own cell or its puppet (out/lab/20261118-095915, -100729); the fixture's cell has
  no weapon, its bite plays no attacker animation (+0x1B0 stays 0 on the attacker too).
* The host's cell given the electric part (`coop.pool borrow electric graft`: the game zaps by itself, animation 0x1E
  on the attacker), both cells next to the same creature: the host's avatar played 0x1E, the guest's own cell never,
  the host's puppet of the guest never; the game's electric step 0xE71520 was never called for a puppet cell
  (`puppet_zaps 0`, out/lab/20261118-102558, -102601, -102744), although the puppet shares the host's model data.
  (The puppet's electric step was the suspected shared cause with BUG-1241, a puppet running its own attack; a hold of
  puppet zaps was written and taken out again for lack of a case.)
* The cell puppets get no animation relay at all (MSG_ANIM is dropped for cells, BUG-0796); what shows a partner's
  attack is the effects relay of world effects near the attacker (8 sent / 8 played per bite round).
* Second round (2026-11-18): both cells armed as players of one species are (`coop.pool borrow jaw|spike graft` on
  both games: a carnivore mouth, part type 2, or a weapon, part type 7, written into each game's model data, so the
  avatar and the partner's puppet carry it), the attacker swims into creatures again and again (the game's contact
  decides the attack), the other player stays next to it: the attacker's own cell played 0x12, 0x30, 0x32, 0x33; the
  other's own cell played none of them on its own game, and every puppet stayed at animation 0 on both games, host
  and guest as attacker (spike: out/lab/20261118-104710; jaw, host attacking: -104707, the guest's half found no
  creature on its screen; the herbivore mouth's chunk bites: -104707 cell_attack_mouth OK).
* On v0.7.14.1 (the scratch worktree SporeCoop-wt/tag/v0.7.14.1-w52evo, the test commands `coop.pool anims` and
  `borrow jaw|spike` patched in, uncommitted): cell_attack_jaw (host half) and cell_attack_mouth the same
  (that tree's out/lab/20261118-104716).
* A cell puppet never animates at all (+0x1B0 stays 0 in every run): what a player sees of a partner's attack is
  only the effects relay (world effects within 1.8 m of the attacker, created at the same place on the other game).
  Inferred, not shown: with two cells on top of each other an attacker's relayed effects are drawn over the other's
  cell, which could look like the other cell attacking.
Not tried: poison clouds of a partner (BUG-0716 makes them in the host's game on purpose), a mouth that swallows the
prey whole, a long session.

Second look (2026-11-18, fix/w53-cellfx; docs/re/cell.md#cell-partner-hatch). Observed:
* The guest fighting far from the host (70-90 m; mouth, jaw, spike, electric, poison; `coop_cell_far_fight.py`, lab
  20261118-124151, -125953, -131202, -132850) changed nothing on the host's own cell: no DamageCell on it, its
  animation +0x1B0 and state +0x18C stayed 0, it turned no more than when idle, no partner effect was played within
  6 m of it. Every world effect id the guest sent, played again where the guest made it (-131202): two of them, and
  only those, change the host's screen.
* cell_egg_L0_autohatch (0xB4B06F12) and cell_hatch_ice (0x082BF046), the hatch of a respawn and of a co-op revive,
  black out the whole screen of the game that plays them, wherever they are (brightness 124 -> 3, -132515), and the egg
  stays drawn over the view for ~3 s.
* A guest knocked out 70 m from the host and respawning (`coop.pool selfhit 20`, nobody near to revive it): the host's
  screen went black (brightness 129 -> 4) and the egg came in, its own cell in the middle (-131912, -132735; the key
  frame is black but for the notices). Both ways bring it: the guest's own hatch is a world effect at its cell and was
  relayed, and the host's on_ko played it on the guest's puppet (KO_RESPAWNED: cell_egg_L0_autohatch; KO_REVIVED:
  cell_hatch_ice).
Inferred for the video: the guest was knocked out in its fight and came back; the host saw its own cell in the hatch's
black screen and whirl ("and I fought the air"). The first report (the partner's cell plays the attack animation) was
never seen: a puppet cell plays no animation at all.

## Fix
`effects.cell_own_only` (effects.cpp `own_only`; cellsync.cpp on_ko): the two hatch effects are neither relayed (sender)
nor played (receiver, also from an older partner) for a partner, and a partner's puppet gets no effect on its revive or
respawn. Off: relayed and played (old). Also: cell_attack_anim's control asked for an attacker animation the fixture's
mouthless bite never plays (it failed in every run); it now checks that the bites land (the creature's hp in the host's
game) and that the other, idle cell plays no new animation.

## Verification
`tools/tests/coop_cell_partner_respawn.py` (suite cell_partner_respawn): the host's game makes no hatch effect and its
screen never goes darker than 35 % of before while the guest, 70 m away, is knocked out and respawns.
* FAIL (`SPORE_FIX_OFF=effects.cell_own_only`): out/lab/20261118-132735 (made 0 -> 1, darkest 3.9 of 130), also
  -131912 (the black key frame).
* PASS: out/lab/20261118-132850 (made 0 -> 0, darkest 117 of 113; the key frame shows the host's cell and the guest's
  respawned one).

## History
- 2026-11-18: opened.
- 2026-11-18 (fix/w52-evoeditor): not reproduced (bites, the electric part); known-issues entry.
- 2026-11-18 (fix/w52-evoeditor): second round (jaw, spike, mouth contact attacks, both directions, v0.7.14.1): still not reproduced.
- 2026-11-18: a second analysis of the video: the HOST's cell swims in empty water and is spun in the combat whirl while the GUEST fights elsewhere ("I was fighting just now" / "and I fought the air"). So the guest's combat interaction or effect is played on the host's own cell. Reopened.
- 2026-11-18 (fix/w53-cellfx): reproduced as a partner's revive / respawn hatch blacking out the other player's screen (the guest knocked out far away); fixed (`effects.cell_own_only`); cell_attack_anim made to measure landed bites.
