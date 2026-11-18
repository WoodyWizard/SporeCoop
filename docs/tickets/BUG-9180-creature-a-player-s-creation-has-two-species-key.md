---
id: BUG-9180
title: Creature: a player's creation has two species keys (its own and the shared one, which also changes with the network id)
status: open
kind: bug
stage: creature
reported: 2026-11-13
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tools/qa_win (manual, TITAN host + a TITAN guest + the MSI as a guest, 2026-11-13, Samuth)
signature:
docs: docs/ROADMAP.md (1b design)
related: BUG-9169 BUG-9173 BUG-9176 BUG-9177 BUG-9174 BUG-9175 BUG-9151
switch: creations.one_key
---

## Report
Roadmap 1b, decisions 2T A, 2V A, 2X A (owner 2026-11-13: "2XA - I feel this is needed for MMO work."). A guest's creation had two species keys: its own My Creations key on its own game, 0x7C + fnv(network id, content) on every other game, and that one changed with each rejoin under another network id.

## Root cause
Observed: creations.cpp shared_instance salted the hash with the owner's network id; share_own already applied the owner's own bundle under that key on its own game, but own_key_here gave the avatar its My Creations key. Consequences: BUG-9169 (the copies of its family another species: converted every second), BUG-9176 (its own species "extinct"), BUG-9173 (a new nest each session), a nest herd of another species.

## Fix
creations.one_key (on): shared_instance hashes the bundle alone (its creation's own key and records: fnv of the packed bundle), the same on every game and across rejoins; a guest's avatar plays that key (own_key_here, Creature: the owner's own applied entry). Kept: the old salted key for the Tribe stand-in villages (one per joiner) and as an accepted founder-archive key (older saves). npc own_family_tick no longer converts species (right after a load it turned the copies into the host's species while the avatar still wore it); it only puts them in the own nest herd (BUG-9175). The nest note (BUG-9173) stays to take over an old save's seat nest once, after the in-session evolution follow in host_tick. Two players with the very same creation file share its species. Not changed: the host's own creation (the world's species keeps its key), cells and UFOs.

## Verification
no-fail-run: manual on two PCs (no Windows lab lane). FAIL (before, earlier 2026-11-13): the MSI's avatar 2920408F, its seat 7CD76455 / 7C5EE376 (changed with the network id), the copies converted (own_family=4), "The Lilku species is extinct!". PASS: the MSI's avatar, its game's player species, the host's "player 1 creature" and its seat all 7C6E4395; its old seat nest taken over once (follows 00000001 -> 7C6E4395); the copies 3DBED5D0/7C6E4395 with no conversion, one offered as mate (class 9, hearts); the MSI left, a TITAN guest joined first, the MSI came back as network id 2: still 7C6E4395, the same nest, no follow; its own editor (coop.vote evolve) on the shared key, a part added, Done: 29210326 -> 7CC05211 on its game, its player species and the host, the nest followed; 0 faults on the three games. Not seen: the click-to-mate after the change (the remote clicks missed the moving mate at 5 FPS; the path is BUG-9175's), Tribe / Civ founder seats under the new keys, an old founder archive loaded.

## History
- 2026-11-13: opened.
