---
id: BUG-1547
title: Creature Rivals: own nest and herd, per-player NPC species relations, influence, PvP and draft
status: fixed
kind: feature
stage: Creature
reported: 2026-11-25
source: docs/11-rivals.md 11.3.2
role: both
versions: 0.8.0
fixed_in: unreleased
branch: w08/rivals-creature
commits: eb9a846b, e8f516d6
tests: rivals_creature, rivals_creature_off
signature:
docs: docs/11-rivals.md, docs/re/creature.md
related: BUG-0794
---

## Report
Implement Creature Rivals mode (WP-R3 of 0.8.0, `docs/11-rivals.md` 11.3.2 and 11.4-11.9): each player owns a separate planet nest and family herd at the configured `BaseDistance` (`Near` / `Middle` / `Far`) with their own species, sees their own nest as home on the guest side, keeps independent per-player wild species relations (with shared wild extinction) and Creature influence actions (`TurnAgainst`, `Protect`, `SmoothOver`, DNA purchase), has war-gated PvP against rival avatars and family members with R7 home-nest extinction protection, keeps independent DNA/brain/parts, and races to brain level 4 (`Tribe` threshold) -> catch-up timer -> Creature part draft and transition.

## Root cause
Creature stage previously only implemented cooperative Together mode: a single shared avatar herd (`mpAvatarHerd` on `cGameNounManager + 0x58`), one shared `cSpeciesRelationshipManager` table synced via `MSG_SPECIES`, pooled Creature parts (`parts.cpp`), and cooperative DNA/progress. Verified GOG 2024 `SporeApp.exe` (`dc04aee5`) RE for R1/R2/R3/R4/R7:
- `cGameNounManager_SetAvatarHerd` (`0x00B1FD10`) sets `[nm+0x58]` (`mpAvatarHerd`); `CreatureGame_JoinNestHerd` (`0x00D40C70`) re-parents the avatar to its assigned home nest herd and moves respawn/nest behavior cleanly.
- `cHerd::mpSpeciesProfile` (`[herd+0xA4]`, raw non-owning pointer) and `cCreatureAnimal::SetSpecies` (`0x00C03920`, `arg3=true` preserves alive/dead state) allow swapping an assigned planet herd and its live family members to a guest's creature creation while keeping existing family members intact.
- `SpeciesRel_MakeExtinct` (`0x00BA4750`) calls `cHerd_SetExtinct` (`0x00C6AED0`, sets `[herd+0x190]=1` and `[nest+0x120]=1`); intercepting `SpeciesRel_MakeExtinct` enforces R7 home-species extinction immunity.
- `cSpeciesRelationshipManager::GetRelationship` (`0x00BA40E0`, already hooked in `tribe.cpp`) lets Creature Rivals return War/Ally/Neutral between players and per-player wild species relations for guest puppets on the host.
- `Creature_StageEndAdvance` (`0x00D2B5F0`) is called by the Timeline Creature->Tribe button (`0x00D2C703`, button `0x06455B55`) before posting message `0x05120262`.

## Fix
Added portable logic in `mods/coop/creature_rivals_logic.h` (with ASan+UBSan unit tests in `tests/creature_rivals_tests.cpp`) and the runtime module `mods/coop/creature_rivals.{h,cpp}` behind `COOP_FIX("rivals.creature", "BUG-1547", ...)`, wired over `MSG_RIVALS_FACTION` (`Stage::Creature`) and Wave 2 callbacks (`register_faction_handler`, `register_npc_influence_callback`, `register_stage_hooks`) with minimal hooks in `social.{h,cpp}`, `parts.{h,cpp}`, `nest.cpp`, `own_progress.cpp`, `npc.cpp`, `tribe.cpp`, and `coop.cpp`:
- R2 host nest selection by `BaseDistance` (`Near` / `Middle` / `Far`), herd species swap to guest's own creature creation, family preservation, and rival nest minimap blips.
- R1 guest perspective swap (`SK_ASSIGN`) setting guest avatar herd/nest to the assigned nest distinct from the host's.
- R4 per-player wild species relation tables on host pushed per guest via `SK_NPC_RELS`, shared wild extinction, Creature influence earning on social wins, `Buy` charging own DNA (`SK_CHARGE_DNA`), and `TurnAgainst`/`Protect`/`SmoothOver`.
- War-gated PvP for rival avatars and family members (`npc.cpp`, `tribe.cpp` `h_SpecRelGet`) plus R7 home nest/species extinction protection (`SpeciesRel_MakeExtinct` hook + `handle_host_rel_event`).
- Own DNA, brain level, and parts isolation (`own_progress.cpp`, `parts.cpp`), Race progress reporting (`SK_PROGRESS`), brain-level 4 win -> catch-up -> Creature part draft (`SK_DRAFT_APPLY`, native part unlock via `parts::unlock_key`, winner payout), and R3 native Creature->Tribe transition helper.
- Feature switch: `coop.fix rivals.creature` (`SPORE_FIX_OFF=rivals.creature` disables Creature Rivals cleanly).

## Verification
- Unit tests & linters: `make -j$(nproc) && make unit-cpp unit-net unit-lint` passed with 0 warnings/errors (`creature_rivals_tests` under ASan+UBSan, `re_lint`, `docs_lint`, `glyph_lint`, `ratchet_lint`, `tickets.py check`, `msg_manifest` all 0 problems).
- Wine lab run `out/lab/20261125-035446` (lane 3, two real Spore instances in Creature stage):
  - `rivals_creature` (ON): **PASS** (39 s, 0 faults) - verified Near (53.8 m) < Far (976.3 m) nest assignment, herd species swap & 4 family members, guest perspective swap (`avatar_nest == assigned != host_nest`), parts (host 54->56, guest 54->54) and DNA (260 vs 115) isolation, per-player wild species relations + shared wild extinction, social influence earning, DNA Buy (260->210), `Protect` blocking `TurnAgainst`, `TurnAgainst` (6->4) and `SmoothOver` (4->6), Neutral blocking vs War allowing rival family hit (7.5->7.5 vs 7.5->4.5), R7 home species extinction blocked (`extinct=0`), brain=4 win -> catch-up -> Creature part draft -> guest native part unlock (54->55) + winner payout (100).
  - `rivals_creature_off` (`SPORE_FIX_OFF=rivals.creature`): **FAIL** cleanly (50 s, 0 faults; `crivals: disabled (rivals.creature is off)`).

## History
- 2026-11-25: opened.
- 2026-11-25: implemented WP-R3 Creature Rivals (`rivals.creature`) with RE verification, ASan/UBSan unit tests, and two-game Wine lab tests (`out/lab/20261125-035446`).
