---
id: BUG-0630
title: Creature: an editor's DNA spend reaches the partner as a negative AddEvolutionPoints and lowers its evolution bar (cPlayer +0x10F0): the guest's bar fell to almost 0, the host's was halved
status: fixed
kind: desync
stage: Creature
reported: 2026-11-06
source: player report 2026-11-06 (Discord, 0.7.8): discord-2026-11-06/M0ng0r-08, M0ng0r-09, Sterben-05
role: both
versions: 0.7.8
fixed_in: unreleased
branch: fix/w45-evo-bar
commits: 28a59ba4, bae4ff10
tests: evo_bar, evo_bar_editors
signature:
docs: docs/re/creature.md#creature-evolution-bar, docs/re/creature.md#creature-bar-spend
related: BUG-0466, BUG-0510, BUG-0128, BUG-0419
---

## Report
Discord report 2026-11-06 (0.7.8, the host on GOG 2024, the guest on Steam; Cell > Creature > Tribe > Civ). Players
call it important.
* The guest (discord-2026-11-06/M0ng0r-08, session s 1981): "slight desync in evolution bar, I'm very close to tier 4
  but the host just got tier 4".
* The guest (M0ng0r-09, s 2722, 6 min after both players' second Creature editor): "our evolution bar just got reset
  to almost 0 or just to 0" (in Discord: from ~90 % to ~3 %; HP and the pack slots stayed).
* The host (Sterben-05, s 2813): "at some point my evolution bar was halved".

## Root cause
Observed (disassembly, GOG 2024 dc04aee5, 2026-11-06, fix/w45-intake): `cCreatureGameData::AddEvolutionPoints`
0xD2E8A0 (cdecl(float delta)) returns when delta == 0, else sets the DNA [0x169E398] = min(DNA + delta, 9999.0
[0x1582E54]) and then adds the **same delta, without a sign check or clamp, to cPlayer +0x10F0** (mCurrentGoalProgress:
the total earned in the stage, the evolution bar; the brain-level and migration thresholds are read from it,
docs/re/creature.md#creature-evolution-bar). `SetEvolutionPoints` 0xD2E480 writes the DNA only. The game itself spends
DNA (the editor's result, 0xD445AA) with SetEvolutionPoints, so in a game of one a spend never lowers the bar.

The mod moves the shared DNA with `game::dna_add_raw(d)` = the original AddEvolutionPoints, for gains and spends alike:
the guest applies `host total - own DNA` of every MSG_DNA (coop.cpp, MSG_DNA), the host applies a guest's MSG_DNA_ADD.
A spend is a negative d, so **every editor spend lowers the other player's evolution bar by the amount spent** (the
spender's own bar stays: its game used SetEvolutionPoints).

Observed (logs, both players, lined up by the session clock):
* s 791 (after the host's first Creature editor): guest `dna: 115 -> 65 cause=unknown`: the host's spend of 50
  applied as AddEvolutionPoints(-50) on the guest: the guest's bar 50 behind the host's from then on. At s 1981 the
  host reached tier 4 and the guest was "very close" (item M0ng0r-08).
* s 2157-2324 (the second evolution vote: both players in their own editors at the same time, the shared pool 755):
  the host's editor left 299 (spent 456, SetEvolutionPoints on the host), the guest's own editor left 444
  (`own editor result: ... remaining DNA 444`, spent 311, `core.dna_spend` sends -311 to the host). The host applied
  the guest's spend: host `dna: 309 -> 0 cause=earned:1-309` = AddEvolutionPoints(-309) on the host (its bar -309:
  "halved"). The host's total 0 reached the guest: guest `dna: 444 -> 0 cause=earned:1-309` = AddEvolutionPoints(-444)
  on the guest (its bar -444, i.e. both players' spends: "reset to almost 0").
* The brain level [0x169E370] only goes up (NestTick), so HP and the pack slots of the reached level stayed, as the
  players saw.
* Inferred: also the shared pool was overspent by 12 (456 + 311 > 755) with both editors open on the same pool: the
  extra was clamped away (BUG-0466 area, not what the players reported).
* Not observed: the bars' values themselves (no log line reads cPlayer +0x10F0 in Creature; the wsync goal row is
  Tribe only).

Class: BUG-0419 (state sent but not applied the way the owner's game has it: the DNA total synced, its side effect on
the bar not).

## Fix
One shared method: `game::dna_add_raw` (game.cpp), the path of every shared DNA change of the mod (the host's total
on a guest, MSG_DNA and the late g_dna_pending; a guest's spend on the host, MSG_DNA_ADD < 0 and the owed spend; the
guest's own editor price, creations.own_editor_dna). A negative change (a spend, anyone's) is written with
`SetEvolutionPoints` (the DNA only, clamped at 0), as the game's own editor does; a gain still goes through the
original AddEvolutionPoints, so the rewards of every player raise every bar as before (and the brain-level /
migration thresholds read the same bar). Switch `creature.bar_spend` (`coop.fix creature.bar_spend off`: 0.7.8).
`coop.dna bar` prints `bar=` (cPlayer +0x10F0); `coop.dna spend N` (SetEvolutionPoints, the game's editor way: tests).

Observed in the lab (not only inferred): with the switch off, a host spend lowered the guest's bar by the spend and
the reverse, two spends at once lowered both bars by both, and in the reported case (both editors at once, a pool of
45, a mouth of 25 each) the host's bar fell 45 -> 25 and the guest's 45 -> 0 (the guest's own editor price was also
charged through AddEvolutionPoints: its own bar lost it too, not only the partner's).

Not covered (inferred, not seen): a host total holding a gain and a larger spend in the same tick reaches a guest as one
net negative change, whose gain part then does not reach the guest's bar (the host's has it). The "overspend by 12"
of the report (two editors on one pool) is clamped at 0 on both games (checked), not prevented.

## Verification
FAIL: SPORE_FIX_OFF=creature.bar_spend evo_bar, lab 20261106-174018 (lane 4: host spends 30, guest bar 130 -> 100;
guest spends 30, host bar 130 -> 100; both 20 at once, both bars 100 -> 80; overspend 80 -> 73).
FAIL: SPORE_FIX_OFF=creature.bar_spend evo_bar_editors, lab 20261106-175633 (lane 5: both editors, host bar 45 -> 25,
guest bar 45 -> 0; also lab 20261106-175242).
PASS: evo_bar, lab 20261106-174215 (lane 4: bars stay 130 on both through every spend, DNA 130 -> 0 the same on both,
gains raised both bars with the DNA); evo_bar_editors, lab 20261106-175613 (lane 4: both bars 45, DNA 0 on both).
Regression: REGRESSION_RUN

## History
- 2026-11-06: opened from the Discord report 2026-11-06 (0.7.8); root cause from the logs and the disassembly (fix/w45-intake).
- 2026-11-06: fixed on fix/w45-evo-bar (creature.bar_spend), tests evo_bar and evo_bar_editors.
