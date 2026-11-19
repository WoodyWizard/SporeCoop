---
id: BUG-1293
title: Creature: a host creature the guest knows by its own remapped key stays invisible: the guest asks the host for a key the host never has (fork BUG-9231)
status: fixed
kind: desync
stage: Creature
reported: 2026-11-19
source: found by the Lineages fork (PR #196), its BUG-9231
role: guest
versions: 0.7.15.2
fixed_in: unreleased
branch: fix/w56-general
commits: 989c2748
tests: npc_host_key
signature:
docs:
related:
switch: npc.species_fallback, creations.ask_host_key
---

## Report
Found by the Lineages fork (PR #196), its BUG-9231: a host NPC of one of the host's own creations stays invisible on a
guest for good. Ours had the identical path: npc.cpp NPC_SPAWN keys the proxy by creations::local_species (the guest's
own key 0x7D...... for the host's creation); when that key builds nothing on the guest the spawn fails "unknown
species" and want_species asks the host for the guest's key, which the host never has ("player 1 asks for species
7D...: not found here" every 30 s). No fallback to the host's key (creations::host_species existed, unused there).

## Root cause
Inferred from the code (the fork saw it live): the ask and the spawn used only the guest's own key.

## Fix
creations.cpp want_species asks by the host's key when given our own key for a host's creation
(`coop.fix creations.ask_host_key`); npc.cpp builds the proxy from the host's key when our key builds nothing
(`coop.fix npc.species_fallback`, a log line once per proxy, `coop.npc` "npc species fallback: host_key_builds=N").
Test command `coop.creations breakremap <host hex>` (our key for that species points at a key nothing registered).

## Verification
- Lab (first 0.7.16 night run, rel/0.7.16 c7699dca, run dirs in the rel/0.7.16 worktree's out/lab/):
  PASS npc_host_key, out/lab/20261119-104136 (the host's new creature of its remapped species 065CBE70 is a proxy on
  the guest, built from the host's key; the host was never asked for the guest's own key).
  FAIL twin (SPORE_FIX_OFF=npc.species_fallback,creations.ask_host_key), out/lab/20261119-105127, for the right reason:
  no proxy on the guest (the detector: "creature 22 key=065CBE70 ... the guest has no proxy"), host_key_builds=0.

## History
- 2026-11-19: opened.
- 2026-11-19: fix committed on fix/w56-general (WP-F1), lab runs pending; found by the Lineages fork (PR #196), its BUG-9231.
- 2026-11-19: fixed (fix/w57-sync): the first lab run passed, its twin failed for the right reason (run dirs above).
