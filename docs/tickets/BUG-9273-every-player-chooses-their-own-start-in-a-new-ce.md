---
id: BUG-9273
title: Every player chooses their own start in a new Cell game: diet, name, colour
status: open
kind: feature
stage: Cell
reported: 2026-11-20
source: owner 2026-11-20
role: 
versions: 
fixed_in:
branch: feat/start-choice
commits: 59cc6a35
tests: tools/tests/coop_start_choice.py (suites start_choice, start_choice_off)
signature:
docs: docs/01-architecture.md#architecture-start-choice, docs/re/cell.md#cell-start-choice
related: BUG-9260, BUG-9263, BUG-9265, BUG-9261
---

## Report
The owner (2026-11-20): when a new game starts, every player starts together on the same planet with their own
creature and chooses for themselves its diet (carnivore / herbivore / omnivore), its name and its colour; the host alone
names the planet. Until now a guest played the host's cell or picked one of its own creations in F9; there was no start
choice per player.

## Root cause
A feature, not a fault. Game side (docs/re/cell.md#cell-start-choice, runtime on lane B): any cell model of the game can
be a guest's own cell through the creations pipeline; the wizard has no omnivore card, and of the game's 44 cell models
two have both mouths (6ED040F4, 0C176D89); the Cell card is decided from the cell data's meal counters (+0x20 grows with
plant meals), which a guest's copy of the host's world inherits.

## Fix
mods/coop/start_choice.{h,cpp} + start_choice_logic.h, the "Your own cell" page in coop_ui.cpp, MSG_START_CHOICE 79
(session 1.5). The host asks a seated guest in its Cell world with no recorded choice; the answer becomes the guest's
own cell (herbivore E219E5D2, carnivore 252A132F, omnivore 0C176D89), its F2 colour and a fresh start of its own food
and meal counters; the host records it per identity in `<planet>.spo.duostart` and sends the table (name tags "Bob
(Glorbo)", a rejoin plays it again without a question). cards.cpp: the diet's Cell card when the guest's game reported
none. Setting `cell.start_choice` (default on); off = 0.7.15 (no question).

## Verification
no-fail-run: a new feature; on a build without it `coop.start` is unknown and tools/tests/coop_start_choice.py fails at
once (both parts). Lane B (Windows, Steam build, 1024x768), new Cell games started from the galaxy menu with the game's
wizard (Hypsov, Logani), and Logani reloaded with its side files wiped for the later runs:
- the guest is asked: its window shows "Your own cell"; it clicked Carnivore, typed "Glorbo", stepped the colour, Start;
- on the host: the guest's cell model 252A132F with carnivore=1 herbivore=0, the colour it picked, the tag "Bob (Glorbo)";
- the guest ate meat (+12 points twice) and not plants (0 points); its own Cell card CFB01B93 after 300 food of meat,
  and the host holds that report for its seat;
- Omnivore: the host sees 0C176D89 (both mouths); the guest ate plants (+12 three times) and meat (+12);
- a rejoin: no question, the host sees the guest's cell again, the guest's own avatar model and colour are back;
- no faults on either game.

## History
- 2026-11-20: opened.
- 2026-11-20: implemented on feat/start-choice; lane B runs passed (carnivore, omnivore, rejoin, setting off).
- 2026-11-20: the body colour (the owner: the colour on the cell's body): a painted copy of the diet's game cell shared
  as the guest's own creation. Lane B: blue carnivore on both games, after a rejoin and after both games restarted and
  reloaded the planet; green herbivore and yellow omnivore copies drawn on both. A first version that packed the model
  again faulted both games in GetCellModelData (the 0xE83EC8 path; not shipped): the colour is now written in place
  (docs/re/cell.md#cell-body-colour).
