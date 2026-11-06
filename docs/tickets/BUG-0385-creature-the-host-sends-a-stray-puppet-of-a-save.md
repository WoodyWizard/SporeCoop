---
id: BUG-0385
title: Creature: the host sends a stray puppet of a saved world as an NPC for a moment; the guest cannot spawn its species (sync-check n missing)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-01
source: player report 2026-11-01 (host log 13:35:01)
role: guest
versions: 0.7.3.3
fixed_in: unreleased
branch: fix/w45-one-game-objects
commits: 3f8ec9a3 450f3ab1
tests: creature_e84 (coop_creature_e84.py, step 2)
signature:
docs: docs/re/creature.md#creature-npc-gaps
related: BUG-0010, BUG-0020, BUG-0339
---

## Report
Host log of a 0.7.3.3 session: `sync-check: MISMATCH ... n: 1 missing on the guest (n142)` once, right after
the host came back from the editor (OK again 30 s later).

## Root cause
Observed in the logs (not reproduced): after the host's editor the host's world held a creature of species
7CA388E5, the host's own key of the guest's creature (the partner's puppet saved with the world). The host
gave it NPC id 142 and sent it; the guest did not know that species ("npc 142: proxy spawn failed (unknown
species) key=7CA388E5") and asked the host, but the host had removed it meanwhile as a stray ("removed a
creature of species 7CA388E5 without tuning in a herd like ours (from a save made while a partner's puppet
existed)") and answered "player 1 asks for species 7CA388E5: not found here". Inferred: the host's NPC
tracking registers such strays for the frames before `remove_saved_strays` runs (once a second).

## Fix
npc.cpp host_tick (`coop.fix npc.stray_ids`, off = 0.7.8): a creature not tracked yet that is such a stray (no tuning
+0xE84, a species profile, a herd like ours: own_kind_herd, the test remove_saved_strays uses) gets no NPC id, so it is
never sent to a guest. Observed in the test (strays removal off to keep it in the world): with the fix off the host
tracks it as NPC #2 at once.

## Verification
creature_e84 step 2 ("[BUG-0385] the host does not track it as an NPC of its world"): FAIL with
SPORE_FIX_OFF=npc.stray_ids out/lab/20261106-020611 (#2), PASS out/lab/20261106-020116. The oracle is the host's NPC
list (what it sends to the guests), not the guest's game: the guest never had a copy (unknown species), the bug only
showed as one sync-check mismatch.

## History
- 2026-11-01: opened.
- 2026-11-06 (fix/w45-one-game-objects, L1): fixed (npc.stray_ids).
- 2026-11-06 (fix/w45-int2): creature_e84 failed in the rel/0.7.9 integration run (out/lab/20261106-040221, lane 3) before
  its BUG-0385 step: "the save brought the puppet back as a game creature without tuning (0)". Observed: the guest's
  puppet was the world's species 065CBE70 (lane 6 runs: the guest's creation 7CB508F6): since BUG-0345 a guest plays
  its own creature only in a world it played it in, so on lane 3 the save held a puppet of the host's species, which
  the load gave its tuning back. No crash: the test printed "(the host crashed ...)" for any missing NPC, and the
  `coop.fix npc.stray_ids off` at s 3.0 is the test's own step 3 (normal run). Fix (test): the guest chooses its own
  creature (creations_state.choose_own, restored at the end), the save waits for the puppet as that creature, the
  message says what is missing. PASS out/lab/20261106-054422, twin FAIL with SPORE_FIX_OFF=npc.stray_ids
  out/lab/20261106-054618 (#5); FAIL before the change on lane 3 out/lab/20261106-051910.
