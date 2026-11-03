---
id: BUG-0457
title: after the host's evolution editor the guest still shows the host's previous creature version
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-02
source: player reports on Discord 2026-11-02 (bug-reports forum, 0.7.3.4, GOG host + Steam guest)
role: guest
versions: 
fixed_in:
branch: fix/w40-cell-evo
commits:
tests: editor_sync_host, editor_sync_host2 (coop_editor_sync.py --case host / host2)
signature:
docs: docs/re/creations.md#creations-host-evolution, docs/re/creations.md#creations-own-editor-dna
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
Update (fix/w40-cell-evo, 2026-11-03), observed: the earlier "the evolution is not applied" was the test's drag
(the part at y 170 is locked in the host's evolution editor too: done on an unchanged creature). With the
unlocked mouth (y 780) `coop.vote evolve` evolves the host: DNA 40 -> 15, species 065CBE70 -> 29141F13
(lab 20261103-160241, 20261103-160839), and again 29141FE7 -> 29141FEC in a second evolution (20261103-161211).
Every evolution gives a new key with records in the host's My Creations; the guest registers the bundle under a
content key (new per version) and its world key, rebuilds the puppet, and its puppet's species is that key with
the host's creature's name (also by eye on the screenshots). The same-key path (cached species profile) did not
happen. Inferred, not verified: the report needs something the lab does not have (another build's parts making
the bundle unusable on the guest, a creature not in the host's My Creations sent key-only, a species changed
under its own key).

## Fix
None: not reproduced on rel/0.7.4 (fix/w40-cell-evo). The reproducer now makes a real host evolution and is in
the suites (editor_sync_host, editor_sync_host2).

## Verification
no-fail-run: not reproducible on rel/0.7.4: editor_sync_host PASS out/lab/20261103-160839 (and 20261103-160241:
checks passed except the old key expectation and the known cold-start caught fault 0x11ECE4F), editor_sync_host2
PASS out/lab/20261103-161211.

## History
- 2026-11-02: opened.
- 2026-11-03: looked at on fix/w40-scale: does not fit the float-field path (a species key and a model rebuild after the host's editor, the creations / world share lifecycle: steps 2 and 3 of docs/sync-architecture-plan.md, causes C and F); left open, no reproduction made (it needs a real edit in the host's editor, coop_editor_desync.py part 5 is the closest test).
- 2026-11-03: fix/w40-editor: reproducer written, the test cannot make the host evolve yet; findings above, not fixed.
- 2026-11-03: fix/w40-cell-evo: the host's evolution made to happen in the test; the guest follows it in 3 runs: not reproduced.
