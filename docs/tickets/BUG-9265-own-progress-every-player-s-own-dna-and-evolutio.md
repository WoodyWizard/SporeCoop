---
id: BUG-9265
title: Own progress: every player's own DNA and evolution bar in Cell and Creature, the stage changes together when every bar is full
status: open
kind: feature
stage: Cell, Creature
reported: 2026-11-20
source: owner request 2026-11-20
role: host, guest
versions: 0.7.15.2
fixed_in:
branch: feat/own-progress
commits: c20f81d5 d066eb71
tests: own_progress_cell, own_progress_creature, own_progress_off
signature:
docs: docs/01-architecture.md#architecture-own-progress, docs/re/creature.md#creature-own-progress, docs/re/cell.md#cell-own-progress
related: BUG-9258, BUG-9260, BUG-9266, BUG-0510, BUG-0315, BUG-1085
---

## Report
The owner's rule (2026-11-20): in the Cell and Creature stages every player has their OWN DNA and their OWN
evolution / progress bar, earned by what they themselves do (eat, kill, befriend). Nobody evolves alone: a player
whose bar is full waits (keeps playing, a notice says it waits for the partner); only when every player's own bar is
full and everybody agrees (the existing I'm ready vote) does everybody move on together. A session setting "Own
progress", default on for this build; off: the old shared total.

## Root cause
A feature. Until now the party had one shared total: the host's DNA (Creature) and food (Cell) were sent to every
guest (MSG_DNA, MSG_CELL_FOOD), a guest's gains were forwarded and paced 1/N (dna_route.h, dna_pace.h).

## Fix
mods/coop/own_progress.{h,cpp} and own_progress_logic.h (portable: the rules, MSG_PROGRESS 77 and the side file),
the session setting all.own_progress (settings_core.h, default 1, coop.ini own_progress), `coop.fix core.own_progress`.
With it on (and creature.mode own):
* every game keeps its own values: no MSG_DNA total, a guest's gain is applied in its own game and not forwarded, no
  pace (1/N), a spend stays the spender's (dna.cpp, session_glue.cpp); Cell: a guest's meal is acknowledged but not
  added to the host's food, the host's total is not applied on the guest, the guest's cell-editor spend stays its own
  (cellsync.cpp, wsync.cpp cell_spent row);
* a guest's kill of a host NPC (the hunt credit) and a guest's social success (paid on the host's game) go to that
  guest as its own DNA (MSG_PROGRESS kGain, social K_RESULT); each game pays its own species goal card;
* the host keeps every guest's last values by identity key (identity.h) per planet, <planet>.spo.duoprogress next to
  the save (founders::write_side, outside the frame); a guest asks for them after every load of a world
  (world::loads_started, ported from Lineages' shared_loads) and gets its food / spent (Cell) or DNA / bar / brain
  level (Creature) back (game::set_brain_level writes SporeCoop::g_BrainLevel for a lower level);
* the stage end: Cell, the host's click on History's check is held until every bar is full and then runs again by
  itself (the switch vote follows); a guest's click is never its own switch. Creature: the host's stage-end request
  (tag 0x64AA168) is refused until every bar is full; then the vote of BUG-1085. Notices: "your bar is full ...
  waiting for X 64%" (again every 60 s), "every player's bar is full ...".
* tests: twoinst.start() switches the setting off for the tests of the shared total (own_progress=True keeps it on).

## Verification
In the game, lane B on Windows (Steam build, instances 3/4, profiles dev3/dev4, new Cell games Calvinis and
Babelix): Cell own meals (host 300 / guest 120: each game's food bar), host full first: its check held, no vote,
the vote starts by itself when the guest fills, both reach Creature; guest full first (Babelix): notice, its check keeps
both in Cell, no vote, then both full -> vote -> Creature; a rejoin keeps the guest's food (100 vs the host's 200).
Creature: own rewards (host +100, guest +60), the guest's kill pays the guest (+1 DNA, bar +1, host unchanged), the
host fully grown first: History's check refused 5 times (stays in Creature, no vote), reload of the planet (both games
restarted, host coop.load) and a rejoin give the guest 125 / 60 / brain 0 back (the save had 3165 / 3100 / brain 4),
both full -> vote -> stage-end editor -> both games in Tribe. Setting off (coop.settings force): the host's gain shows on
the guest (shared total). No faults in the logs.
no-fail-run: a new feature; on main `coop.progress` is unknown and every part of coop_own_progress.py fails at its
first check; --part off is the setting's twin. The Linux lab run is left to the orchestrator.

## History
- 2026-11-20: opened.
