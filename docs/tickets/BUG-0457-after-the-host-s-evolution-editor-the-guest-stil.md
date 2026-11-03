---
id: BUG-0457
title: after the host's evolution editor the guest still shows the host's previous creature version
status: open
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in:
branch: fix/w40-editor
commits:
tests: coop_editor_sync.py --case host (reproducer, not in a suite: the host evolution does not happen yet)
signature:
docs: docs/re/creations.md#creations-own-editor-dna
related:
---

## Report
Host's mark (s 1334, 17:46): 'after evolve editor in creature stage my species is desynced, he sees previous version of mine (only on his screen)'. The puppet keeps the old model after the host's species changes in the editor (creations update after an evolution editor). Class G2 (BUG-0419).
Target: 0.7.4.
<!-- what the player (or the test) saw, in our words; no player names, no paths from their PC -->

## Root cause
Not found yet (fix/w40-editor, 2026-11-03). Observed:
* `coop.vote editor`, the helper every editor test uses (guest_editor, editor_strays, migration,
  vote_*), opens the "edit a creation" editor for the host too (a request without the evolution tag):
  the host added a mouth (40 -> 15 shown), done, and its species stayed 065CBE70 with DNA 40
  (lab 20261103-141304, 20261103-141812). So no test covered a host evolution inside the Creature stage;
  only the first editor after Cell (the game's own request: legless_guest, editor_desync) did, and there
  the host's key changes (k0 -> k1) and the guest showed it.
* A native-like request (`coop.vote evolve`: +0x90 0x64AA168, +0x94 the collected parts, flags,
  +0x70 cPlayer +0x10FC..+0x1108, 0x1686AF0 = 1, as 0xD42E50 builds it) went through the vote, the
  host added a mouth, done, "evolved the species", hearts at the nest; GetEvolutionPoints stayed 40 and
  the avatar's key 065CBE70 for 60+ s (lab 20261103-142443). The evolution result was not applied:
  inferred, the game's own path needs state our request lacks (the mate scene, this+0xDA, the stage's
  state machine 0xD43DE0 that runs the return: SetSpecies from data +0x18 at 0xD44545, DNA from +0x34).
Inferred (not verified): the fixture's host species is a stage species, sent as a key-only bundle
("player 0 creature = 065CBE70 (0 records)"). If an evolution keeps the key and edits the species in
place, share_own's bundle never changes and the guest's species manager keeps its cached profile of
that key: the puppet keeps the old model (the report's "previous version").
Next step: drive the real mating call at the nest (Backspace, the mate, docs/re/creature.md) or post
0x4471C60 to the creature game with +0xDA set, then compare the host's avatar key before / after.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-02: opened.
- 2026-11-03: looked at on fix/w40-scale: does not fit the float-field path (a species key and a model rebuild after the host's editor, the creations / world share lifecycle: steps 2 and 3 of docs/sync-architecture-plan.md, causes C and F); left open, no reproduction made (it needs a real edit in the host's editor, coop_editor_desync.py part 5 is the closest test).
- 2026-11-03: fix/w40-editor: reproducer written, the test cannot make the host evolve yet; findings above, not fixed.
