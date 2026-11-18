---
id: BUG-9119
title: progress debt instead of losing DNA or food when a downed player is not revived
status: fixed
kind: feature
stage: Creature Cell
reported: 2026-11-11
source: owner request on 0.7.8-lineages47-dev (after BUG-9118)
role: all
versions: 0.7.8-lineages47-dev
fixed_in: 0.7.8-lineages48-dev (progression-based cost: 0.7.8-lineages50-dev)
branch: feat/lineages-47
commits:
tests: tests/progress_debt_tests.cpp; QA host 9 + guest 8, Samuth.spo (Creature) and Cellfive.spo (Cell), see Verification
signature: -
docs: docs/06-testing.md#test-progress-debt
related: BUG-9118 BUG-9092 BUG-0510
switch: core.progress_debt
---

## Request
"Let's introduce the loss of progression as progress debt. Overlay the progress bar with a deep
slightly desaturated red that needs to be refilled before they can gain DNA again. They never lose
DNA, they just need to regain their progress. Figure out where this can be applied besides Creature
and Cell." Cell's cost: option 1, 10% of the editor's DNA.

## Change
* `progress_debt.{h,cpp}`, `progress_debt_logic.h` (pure: costs and the ledger). The host keeps one
  ledger per stage and world (another stage or save clears it; an editor or a load screen in between
  keeps it) and announces it with MSG_PROGRESS_DEBT (81: u32 mode, u32 world, f32 debt) on a change
  and every 2 s.
* Creature: creature_rescue `charge()` adds ceil(10% of the DNA) as debt; nothing is subtracted.
  Every host gain repays it first: the host's own (on_dna), a guest's (MSG_DNA_ADD), owed DNA, a
  social friend, goal rewards (social.cpp). A gain that the debt eats whole does not reach the game
  (no DNA, no bar movement).
* Cell: a respawn after the timer or a give-up (own: downed_tick; a guest's: on_ko KO_RESPAWNED on the
  host) adds 10% of the editor's DNA (DNAForFood(food) - spent), in food at the stage's average rate
  (food x dna / DNAForFood(food)). While the party owes, every meal goes to the host (earned_food: a
  guest reports it raw, as with a campaign workload), which repays the debt first.
* The overlay (SK_EV_OVERLAY): the bar's own pixels in the debt's stretch are copied from the back
  buffer (StretchRect into a per-frame render-target texture, released the same frame, so nothing of
  ours is in D3DPOOL_DEFAULT at a device reset) and drawn again through a ps_2_0 shader compiled with
  the system's d3dcompiler_47 (or _43): green pixels become a deep, slightly desaturated red with the
  same brightness (tick marks and shine stay), grey or white ones (a brain-level clip, the arrow) stay.
  Without a compiler: a flat red strip. Placement: from the fill's end back by debt x the bar's average
  pixels per point, at least 4 px. The fill's end is the game's own marker window 0x051B5168 (the
  green arrow; the Creature bar is not linear: each brain level has the same width and its own DNA).
  The bar is HUD window 0x051B5478, its fill 0x0652A628 (both stages), found outside the frame every
  0.25 s while a debt exists; hidden HUD (editor, death screen) = no strip.
* Notices: "<name> gave up. Progress debt: 3 DNA (new DNA repays it first).", "... respawned small
  next to X. Progress debt: N food (new food repays it first)", "Progress debt repaid: DNA counts
  again." A guest's gain: "Guest earned 6 DNA (repays the progress debt)".
* Off (`coop.fix core.progress_debt off`): the old loss (Creature 10% DNA; Cell none).
* Not kept across a session's end or another save (the ledger is the host's memory).

## Other stages (owner question)
* Tribe: the last villager's 10% food setback could become a food debt (gathered food repays it).
  The Tribe goal bar counts allied / conquered villages (whole steps), so a debt fits the food
  counter, not the bar. Candidate for a follow-up.
* Civ: no downed state; its bar counts cities. No fit.
* Space: the ship is destroyed the native way (native penalties); no progress bar to owe on. No fit.
* Adventure: no DNA; a captain who is not revived gets back up beside a partner. No fit.

## Verification
Creature, QA host 9 + guest 8 (Samuth.spo), the guest 30 m away:
* `coop.giveup down` + `coop.giveup` on the guest: "Guest gave up. Progress debt: 3 DNA (new DNA
  repays it first)." on both, DNA 29 on both before and after, `coop.debt` owed=3 on host and guest.
* `coop.dna gain 2` on the host: DNA 29, debt 1 (both). `coop.dna gain 5`: DNA 33, debt 0, "Progress
  debt repaid: DNA counts again." on both; the marker moved 596 -> 601.
* `coop.debt add 30`: the red stretch on both screens, ending at the green arrow (edge 601).
* `coop.dna gain 6` on the guest: "Guest earned 6 DNA (repays the progress debt)", debt 30 -> 24,
  DNA 33 on both.
* Switch off: the same give-up took 4 DNA (33 -> 29) and left no debt (the old behaviour).
Cell, QA host 9 + guest 8 (Cellfive.spo), the guest 20 m away:
* `coop.pool selfhit 20` + `coop.giveup` on the guest: the host logged "ko: player 1 cell state 1 -> 3",
  "Progress debt: 26 food (new food repays it first)." on both; food stays 204 on both; the red
  stretch on both screens back from the arrow at 458.
* `coop.pool food 10` on the host (an own meal, paced for two): debt 26 -> 23, food 204.
* `coop.pool food 80` on the guest: the meal went to the host, repaid 23, "Progress debt repaid: food
  counts again.", the rest (+6) made the food 210 on both; the guest's food never went up and back.
* No crash files, 0 faults.
* Host unit test progress_debt_tests: pass.

* Owner review: the first flat strip lost the bar's tick marks and shine; the recoloured stretch keeps
  them (QA host 9, `coop.debt add 30`: tinted=32 shader=1, the clip at the brain level stays grey).

## History
- 2026-11-11: implemented on feat/lineages-47.
- 2026-11-11: owner: progression debt, not DNA. A death now owes 10% of the stage's progress so far (progress_cost: Creature cPlayer +0x10F0, Cell food), at least 1; QA Samuth.spo: 321 progress -> debt 32, DNA 33 unchanged on both games. Before (48-49): 10% of the unspent DNA, 0 once it was spent ("not stacking").
