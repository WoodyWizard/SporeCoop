---
id: BUG-0389
title: Creature, Cell: shared DNA names its cause (notice and DNA cause ledger)
status: fixed
kind: feature
stage: Creature
reported: 2026-11-01
source: player request 2026-11-01 (BUG-0351)
role: both
versions: 
fixed_in: 0.7.3.4
branch: fix/w31-creature-gate
commits: d89e8c5
tests: dna_notice, dna_notice_cell (tools/tests/coop_dna_notice.py), validate_tests (dna_ledger)
signature: dna: <a> -> <b> cause=<cause>:<player><+amount>
docs: docs/re/creature.md#creature-dna-causes, docs/06-testing.md#testing-dna-causes
related: BUG-0351
---

## Report
Players (2026-11-01): "we get randomly a ton of DNA in Cell and Creature". BUG-0351 found the shared
pack DNA (Creature) and the shared food (Cell) by design plus the game's editor refund; nothing told
the players where a gain came from.

## Root cause
By design (docs/03-gameplay.md): the host's game pays every gain of the pack and sends the total
(MSG_DNA, a bare number); a guest's kill of a host NPC is paid as the host avatar's (npc.cpp kill
credit), so neither the guest (no reward popup) nor the host (a popup for "its" kill) could tell.

## Fix
A feature, no fix switch (nothing old to keep; an older host sends no causes: the guest writes
`cause=unknown` and shows nothing). mods/coop/dna_ledger.h + coop.cpp, npc.cpp, cellsync.cpp:
* the host marks each gain: `host` (its game's own: its or its pack's hunts, meals, friends, goal
  cards), `hunt` (a guest's credited kill: gains within 2.5 s of the credit), `earned` (a guest's own
  gain, MSG_DNA_ADD), `owed`; MSG_DNA gets an optional tail u8 n x {u8 cause, u8 player, f32 amount};
* every machine logs `dna: <a> -> <b> cause=hunt:1+9,host:0+5` (Creature) and `dna: food <a> -> <b>
  cause=meal:<player>+N` (Cell; a guest writes 255 = some partner), for the monitor's DNA ledger;
* a notice (the co-op notification panel, kind good), gathered per source for 1.5 s (Cell meals 15 s),
  at most one every 4 s: host "+9 DNA from Bob's hunt", "+10 food (DNA) from Bob's meals"; guest
  "+9 DNA from your hunt (paid by the pack)", "+7 DNA shared by Alice's pack", "+6 food (DNA) from
  your partners' meals". The existing "Bob earned N DNA" and owed lines stay.

## Verification
no-fail-run: a new feature (no old behaviour to fail); before it no notice and no `dna:` ledger line existed.
PASS: dna_notice lab 20261101-095738 and 20261101-095926 (host "+9 DNA from Bob's hunt", guest "+9 DNA
from your hunt", guest "+7 DNA shared by Alice" with the host's paid amount, logs hunt:1+9 / host:0+7 on
both, DNA equal); dna_notice_cell lab 20261101-100205 (host "+10 food (DNA) from Bob's meals", guest
"+6 food (DNA) from your partners' meals", logs meal:1+10 / meal:255+6). Screenshots
out/lane6/shots/dna_notice_*.png.

## History
- 2026-11-01: opened and implemented (fix/w31-creature-gate).
- 2026-11-02: released in 0.7.3.4.
- 2026-11-02 (0.7.3.4, host's mark in Cell): 'Getting a lot of DNA from guest like 40-60 randomly for eating in cell stage': the shared food is by design and the notice shows it; players still read it as random: make the Cell notice clearer (amount and source in one line) in 0.7.4.
