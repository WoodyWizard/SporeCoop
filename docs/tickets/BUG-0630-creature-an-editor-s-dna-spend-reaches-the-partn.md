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
commits:
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
<!-- Proposed: move the shared DNA so the bar follows only gains: a negative delta (a spend, anyone's) through
     SetEvolutionPoints (DNA only), a positive one through AddEvolutionPoints as now; better, the host sends its
     cPlayer +0x10F0 with MSG_DNA and the guests set it (one bar, the host's), the way wsync's goal row does in Tribe.
     Behind a switch (creature.bar_spend). -->

## Verification
<!-- Lab: two games in Creature (fixture after the first brain level), read cPlayer +0x10F0 on both (`coop.wsync`
     dump or a new `coop.dna bar`), the host opens its editor and spends 100 (or `call
     Simulator::cCreatureGameData::SetEvolutionPoints`), check the guest's +0x10F0 unchanged; the guest's own editor
     spend, check the host's +0x10F0 unchanged; both editors at once (the reported case). Without the fix the
     partner's bar falls by the spend. -->

## History
- 2026-11-06: opened from the Discord report 2026-11-06 (0.7.8); root cause from the logs and the disassembly (fix/w45-intake).
