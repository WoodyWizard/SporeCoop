---
id: BUG-9231
title: Creature: some enemies are invisible on the guest
status: fixed
kind: bug
stage: creature
reported: 2026-11-15
source: owner play test 2026-11-15
role: guest
versions: 0.7.14.1-lineages58-dev
fixed_in: unreleased
branch: feat/lineages-47
commits: 5bfa5c88
tests: manual Windows QA (TITAN host + guest, coop.creations breakremap)
signature:
docs:
related:
---

## Report
Owner play test 2026-11-15 (host Pandize on TITAN's Steam Spore, guest BigTexas over the internet, 58-dev): "Some enemies BigTexas was unable to see."

## Root cause
Observed in the host's own log of that session (read only, 2026-11-15 17:48-17:54): "creations: player 1 asks for species 7D8CD433: not found here", then 7D535D52, every 30 s for 6 minutes. 0x7D...... is the guest's own key of a host's own creation (creations apply_own_species: g_remap host key -> ours, g_unmap back); a proxy of such a species carries ours (npc MSG_NPC_SPAWN: local_species). When the guest's game could not build ours (no species profile for that key), the proxy spawn failed with "unknown species" and want_species asked the host for OUR key, which the host never has: no answer, the proxy never built, the enemy invisible for good.
Inferred (no guest log): why ours was not buildable there (the guest's package for it not answering). Either way the host's key works: apply_own_species registers the host's records under the host's key too when the guest has nothing there.

## Fix
- creations.ask_host_key: want_species asks by the host's key (g_unmap) when it is given ours.
- npc.species_fallback: a proxy whose key (ours of the host's creation) is not buildable is built with the host's key; logged once per proxy ("not buildable here: built with the host's key (BUG-9231)").
- Tests: `coop.creations breakremap <host key>` (guest) points ours of a host's creation to a key nothing registered.

## Verification
no-fail-run: not reproduced on TITAN yet (the QA worlds' host creations build under ours); the next play test's guest log shows the fallback line if it happens.

## History
- 2026-11-15: opened.
- 2026-11-16: cause found in the host's log; fixed (ask by the host's key, build with it).
