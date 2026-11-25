---
id: BUG-1561
title: Creature & Tribe: guest befriending creatures and tribes fails in co-op
status: fixed
kind: bug
stage: Creature
reported: 2026-11-25
source: Discord (#bug-reports, Smile / Szmajol)
role: both
versions: 0.8.0.1
fixed_in: 0.8.0.2
branch: w0802/guest-befriend
commits: eb852dcf
tests: tribe_tests, guest_befriend_tribe, guest_befriend_tribe_off, guest_befriend_creature, guest_befriend_creature_off
signature:
docs: docs/re/creature.md, docs/re/tribe.md
related: BUG-0748, BUG-1256
---

## Report
Discord (`Smile / Szmajol`):
- Creature stage: Player 2 (guest) had a hard time befriending wild creatures and creatures of the host's own species in co-op.
- Tribal stage: Player 2 (guest) could not befriend NPC villages at all, even when the host gave all soldiers/tribe members to Player 2.

## Root cause

### Tribal Stage (5 blocking bugs in Together mode)
1. **`ChooseInputActionID` (`0x00CCF130`) blocked guests without the host's `mpLeader`:** Right-clicking a neutral/friendly NPC villager in Social Mode (`TribeUI+0x390 == 0`) checks `cCommunity::GetLeader` (`playerTribe + 0x11C`). Because the host owns the single native `mpLeader` and `h_SetSelected` refuses foreign members, every guest click returned `0x2B` ("Select your Chieftain to socialize!") instead of `0x27` (`CitizenAction 26`).
2. **Guest HUD ability bar never rebuilt for synced instrument racks:** When the host bought instrument racks (tools `4/5/6`: Didgeridoo, Maracas, Horn) and mirrored them to the guest via `make_tool_in`, `SporeCoop::AbilityBar_Rebuild` (`0x00D49400`) was never called on the guest, leaving row 0 empty (`-1`) so keys `1`/`2`/`3` and HUD buttons did nothing.
3. **Guest instrument keys and HUD buttons never played on the host's `SocialSession`:** `AbilityBar_Status` (`0x00D49220` -> `0x00D1FB30`) and `AbilityBar_Use` (`0x00D49560` -> `0x00D1FE00`) only checked locally selected citizens in BT node `0xB46E8803` and a local `SocialSession`. Because guest mirrors skip AI (`h_BTTick`), status always returned `0xE`/`0xF` and nothing was sent to the host. When the host gave all soldiers to Player 2, neither player could play a single note (`duet_tick` only ticks when `owners.size() >= 2`), so the performance always timed out and failed (`state 6`).
4. **No Tribal social progress meter on the guest:** The native Tribal social progress UI (`[TribeGameUI + 0x50]`) was never synced or shown on the guest during performances.
5. **BUG-1068 mission hook skipped `CitizenAction 26`:** `tribe_host.cpp` only registered the village goal mission (`wsync::goal_target`) for raids (`15`) and food gifts (`9`), not social performances (`26`).

### Creature Stage (`mods/coop/social.cpp`)
1. **Engaged host NPC was not held still or facing the guest:** `host_engage` and `host_prompt_tick` left the host NPC in its normal wander/follow-host BT, so nest mates and wild NPCs walked out of `kActRange` (12 m / 16 m) during the guest's social game and caused silent refusals.
2. **Host's own species not guaranteed allied (`relation = 6`) for own-species guests:** In `creature.mode own`, if the host's native `SpeciesRelationshipManager` table lacked an explicit `relation = 6` entry for the host's own species (e.g. fresh stage or newly evolved host species), `enforce_table` reset the guest's record to `0`, preventing `CreatureSocialMode` from returning `4` (recruitable) on the host's nest mates.
3. **Host scored guest answers from host puppet abilities instead of guest's own ability stats:** When the host's puppet was a fallback or mismatched species (or when the guest had pack members), `host_act` fell back to `1.0` points and zero pack bonus, making Tier 2+ social games fail.
4. **Target switching & stale engagement lockouts:** `h_Post` skipped re-sending `K_ENGAGE` when re-clicking the same `g_engaged` NPC for 60 s, `host_prompt_tick` erased expired games without sending `K_RESULT(false)`, and `h_BarUse` intercepted clicks on a newly targeted NPC proxy as actions on the previously engaged NPC.

## Fix

### Tribal Stage (`COOP_FIX("tribe.guest_social", "BUG-1561", ...)`)
- `mods/coop/tribe_logic.h`, `mods/coop/tribe.cpp`, `mods/coop/tribe_host.cpp`:
  - Override `ChooseInputActionID` `0x2B` -> `0x27` via `tribe_logic::choose_social_override` when a guest (or non-leader owner) has living own members selected in Together mode.
  - Queue `SporeCoop::AbilityBar_Rebuild` outside the frame on `make_tool_in` (and in guest tick if row 0 is empty while instrument racks exist).
  - Broadcast live `SocialSession` state (`active`, `state`, `can_play`, `rel_level`, `req_inst`, `outcome`, `progress`, performing member IDs/tools/cooldown readiness) from host to guest via `MSG_TRIBE_ANIM` sub-kind `22` (`kAnimTribeSocial`).
  - Drive the guest's native `[TribeGameUI + 0x50]` social meter UI outside the frame (reset, relation faces, progress fill, win icon / fail drain, clean hide on end or session/world teardown).
  - Hook `SporeCoop::AbilityBar_Status` and bridge `SporeCoop::AbilityBar_Use` (via `social::set_tribe_bar_use`) on the guest so instrument slots `3`/`4`/`5` (keys `1`/`2`/`3` and HUD buttons) reflect session readiness and send `MSG_TRIBE_CMD` sub-kind `5` (`kCmdSocialPlay`) to the host; host validates ownership, BT `0xB46E8803`, tool match, `can_play`, and cooldown, then posts native `BT_PostStimulus(ai + 8, 0x10000000, 0, 10.0f, nullptr)` with `stim+0xC = ability_idx`.
  - Include `CitizenAction 26` in the BUG-1068 village goal mission hook (`tribe_logic::guest_mission_action`).

### Creature Stage (`COOP_FIX("social.guest_befriend", "BUG-1561", ...)`)
- `mods/coop/social.h`, `mods/coop/social.cpp`:
  - Hold the engaged host NPC still (`game::stop`, anchor pull-back if >1.5 m) and facing the guest's puppet (`game::set_orientation`) during an active guest social game; release any prior unfinished game for the guest when engaging a new NPC.
  - Ensure host `MSG_SPECIES` always includes the host avatar's own species key with `relation = 6` in Together mode.
  - Append guest ability `(key, pts)` as optional trailing bytes in `K_ACT`; host sanitizes/clamps `(key, pts)` and adds authoritative pack-member social bonus from `npc::pack_owners_snapshot()`.
  - Allow `h_Post` to re-send `K_ENGAGE` on stale repeat clicks, notify guest via `K_RESULT(false)` when `host_prompt_tick` expires a game, and check `cCreatureBase::GetAnimalTarget` (`av+0xE7C` -> `vt 0x5C(0xCE9F6639)`) in `h_BarUse` so targeting a new NPC passes through to `o_BarUse`.

## Verification
- Build & unit/lint suites: `make -j$(nproc)`, `make unit-cpp unit-net unit-lint` (including new `tribe_tests` + `--nofix` cases for `tribe_logic.h`), `python3 tools/tests/test_lint.py` all PASS with 0 problems.
- Wine lab (lane 4):
  - `20261125-164407`: `guest_befriend_tribe` **OK** (75 s, 0 faults: bar rebuilt, `ChooseInputActionID` returned `39`, guest meter UI opened/tracked to `1.001` win with `duet_ticks=0` and hid cleanly); `guest_befriend_tribe_off` **FAIL** (105 s: `bar_rebuilds=0`, `action=43` on all villagers, `ui_vis=0`, `sent=0/host=0`, `max_prog=0.074` failed); `guest_befriend_creature` **OK** (56 s, 0 faults); `guest_befriend_creature_off` **FAIL** (55 s: `rel=0`, `npc_holds=0`, `guest_pts=0`).
  - `20261125-170104`: final DLL confirmation: `guest_befriend_creature` **OK** (55 s), `guest_befriend_creature_off` **FAIL** (55 s), `guest_befriend_tribe` **OK** (73 s).
  - Regressions on lane 4: `20261125-164936` (`tribe_duet` **OK**, `guest_pack_recruit` **OK**, `creature_social_prompts` **OK**, `creature_social_meter` **OK`) and `20261125-165813` (`creature_social_bar` 3/3 **PASSED** cold + warm).
