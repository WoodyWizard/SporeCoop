# SporeCoop 0.8.0.1 Plan

Patch release addressing all P0/P1 real-gameplay blockers uncovered by the independent v0.8.0 static audit, the first end-to-end `campaign_rivals` bot campaign, Influence & Relations F9 UI parity, critical Shared Adventure Editor fixes, and any bugs found by the 4-player `=campaign` run.

## Work Packages

### WP-1 (`w0801-core`, `BUG-1551`): Rivals Core, Persistence, Notices, F9 Influence/Relations UI, Lobby Overlap & Security/Tooling
- Stop resetting `g_state.race` and `g_state.inf` on host editor round-trips (`rivals.cpp:684-703`: compare `to_s != g_state.race.stage`).
- Fix `rivals.cpp` persistence (`g_store_serial` reset in `SESSION_STATE`, load existing record in `disk_opened()`, clear record in `disk_save()` when returning to `AllTogether`, store player keys in `PersistedWorld::history`).
- Stop duplicating Rivals notices (`rivals.cpp` + `coop.cpp:3053`: avoid double `MSG_NOTICE` + guest bounce-back on `K_EVENT`).
- Check stage callback return value in `Protect` (`rivals.cpp:447-456`) and verify actor is allied with `npc_id`.
- Fix 3-4 player disconnect race completion (`rivals.cpp:663-673`, `rivals_logic.h:705-737`).
- Guard OOB array reads (`rivals.cpp:1546` `chronicle add <pid>`, `rivals.cpp:1187,1247-1255` `ui_rows()`).
- F9 Rivals UI parity: hide `Buy +25` in Cell, add Decline Ally / Decline Peace buttons, show error feedback toast on rejected actions, and render stage-provided `on_npc_ui_list` entries with clickable `Turn (30)`, `Protect (20)`, and `Smooth (25)` buttons + active protection countdowns.
- Fix lobby `D.vote_mode_btn` overlap (`coop_ui.cpp:1289,2545`), `Tribe -> Civ` vote string (`vote.cpp:28,945`), and Cell Rivals `own_end_hold` (`vote.cpp:1103-1105`) so host cannot end Cell before catch-up/draft finishes.
- Fix pre-auth password eviction (`net/session.cpp:1455` `if (!pw_ok) continue;`), `std::isfinite` guard on `K_UFO` rx (`player_rules.cpp:1147-1156`), and `tools/lab.py:1344` `lanes` subcommand `TypeError`.

### WP-2 (`w0801-cell-crash`, `BUG-1552`): Tribe Crash Guard (`modes::rts_anchor`), BUG-0359 Tripwire & Cell Rivals Fixes
- Gate `modes::rts_anchor` (`mods/coop/modes.cpp:1642-1669`) strictly on `kTribe` (`mPlayerTribe`) and `kCiv` (`GetPlayerCivilization`) so Tribe never crashes calling `GetPlayerCivilization`.
- Add one-shot diagnostic tripwire in `game::for_each_object` (`mods/coop/game.cpp:503`) when `!in_exe(*(void**)obj)` to date any `BUG-0359` (`0x3F028F5C`) overwrite.
- Use `take_pending_draft_payout_for(Stage::Cell, pid)` in `cell_rivals.cpp:637-643` so Cell never steals Creature draft payouts.
- Trigger Cell->Creature transition cleanly when a guest wins Cell first (never strand host).
- Apply `BUG-1538` spent-DNA clamp to Cell Rivals food rollback (`cell_rivals.cpp:508-509`).
- Fix 3-4p Cell draft item filter (`cell_rivals_logic.h:234-252`) and poison cloud PvP owner attribution + lethal poison meat drops (`cellsync_combat.cpp:568-608`).
- Fix lagging guest's empty Cell tide pool during extreme `host_scale / own_scale` divergence in Cell Rivals catch-up (`cellsync_guest.cpp:428`) and `Drawn.in_scale` quantization slack in `tools/tests/monitor.py:640-684`.

### WP-3 (`w0801-creature`, `BUG-1553`): Creature Rivals Enum, War PvP, Family Combat, Pack Recruit, Arrows, Transition & Influence NPC UI
- Fix `GetRelationship` enum inversion (`creature_rivals_logic.h:29-35,272`, `creature_rivals.cpp:871`): `1 = scared`, `2/5 = hostile/aggressive`; never mark relation `5` predators as `kRelExtinct`.
- Fix Creature War PvP & family combat: `puppets.cpp:172,223-260` War hover/collision; `settings_core.h:122-133` disable `kPackRescue`/`kRevive`/`kPackHeal`/`kJointHunt` in Rivals; `npc.cpp:1953-1979` stop clearing host pack targets at War; `npc.cpp:1321,2448-2450,3791` separate host `NF_FAMILY` proxies from guest's swapped `avatar_herd()`.
- Fix guest pack recruitment (`social.cpp:1042-1043,1073,1103`) to check guest's own relation table & species and isolate `impressed`; lower guest's wild relation on guest `event == 2` attacks (`creature_rivals.cpp:791-794`).
- Enable partner edge arrows in Creature Rivals (`find.cpp:246-247`).
- Wire `trigger_native_tribe_transition()` automatically when Creature race & draft end (including guest win first).
- Throttle `host_assign_nests` scan (`creature_rivals.cpp:1205`) once active players have nests.
- Implement `on_npc_ui_list` in `creature_rivals.cpp` for F9 Influence UI (`Turn`/`Protect`/`Smooth` on wild species).

### WP-4 (`w0801-tribe`, `BUG-1554`): Tribe Rivals Attack, Tool Buy, `wsync` Isolation, Tags/Relations, Draft Payout & Influence NPC UI
- Bypass `ff_try_order` in `tribe_host.cpp:706` when `tribe_rivals::active()` so guests can attack host tribe members at War.
- Route guest tool/hut purchases (`worldobj.cpp:576-585`, `tribe.cpp:1642`) to sender's `tribe_of_player(from)` and charge sender's food.
- Gate `wsync.cpp` `s_tlevel` (`1006-1024`) and `s_goal` (`405-434`) on `!tribe_rivals::active()`, stop pushing guest raids/gifts into host `wsync::goal_target` (`tribe_host.cpp:712`), sync host village food to guests, AND gate `wsync.cpp` `s_nest` (`928-982`) on `!creature_rivals::active()`.
- Write player-vs-player War/Ally relations into `cRelationshipManager`, include rival tribe members/chiefs in `tribe::for_each_owned` / `chiefs_tick` for tags/arrows, disable co-op `epic_tick` (`tribe.cpp:4393-4414`) in Rivals, and deliver Creature Rivals draft payout (`take_pending_draft_payout_for(Stage::Creature, pid)`) as Tribe food.
- Implement `on_npc_ui_list` in `tribe_rivals.cpp` for F9 Influence UI (`Turn`/`Protect`/`Smooth` on NPC tribes).

### WP-5 (`w0801-civ-space`, `BUG-1555`): Civ Rivals & Space Rivals Gameplay, Transitions, Economy & Influence NPC UI
- Fix Civ Rivals 1 s NPC diplomacy overwrite (`civ_rivals.cpp:710-738`): read back native diplomacy changes instead of wiping them every second.
- Fix Civ Rivals `SpendMoney` on guest NPC-flagged civ (`civ_host.cpp:1086,1107`, `civ_rivals.cpp:657`) so spending deducts instead of refilling NPC budget.
- Deliver Tribe Rivals draft payout (`Stage::Tribe`) as Civ money on Civ entry, and Civ Rivals draft payout (`Stage::Civ`) as Space money on Space entry.
- Fix Space Rivals natural `Civ -> Space` liftoff stranding (`space_rivals.cpp:828-831,857-862,903-912`): ensure deferred post-liftoff retry runs `teleport_in_galaxy` / `travel_to_star`.
- Wire `civ_rivals` guest city counts automatically into `space_rivals::set_guest_civ_cities` for all players `1..3`.
- Scale Space influence buy price (`space_rivals.cpp:1011-1039`) to Space economy instead of raw 50/75/100 Sporebucks.
- Keep `space_rivalry.cpp:511,1119,1292` toggle synced with `rivals::set_stage_mode(Stage::Space, ...)` and retry `sync_from_rivals()` when `money_ptr()` becomes available.
- Implement `on_npc_ui_list` in both `civ_rivals.cpp` and `space_rivals.cpp` for F9 Influence UI (`Turn`/`Protect`/`Smooth`).

### WP-6 (`w0801-adveditor`, `BUG-1556`): Shared Adventure Editor Real-UI Hardening
- Broadcast host session reset when `draft_key` changes on Save or host leaves/re-enters edit mode on Play-test (`adv_editor.cpp:783-813,1411-1414`), so guests clear `synced` and re-sync instead of dropping ops forever.
- Send explicit `LEASE_RELEASE` after drag flush / `h_CommitHistory` (`adv_editor.cpp:889-911,1170-1175`) instead of leaving a 5 s post-drag lockup.
- Snap back rejected guest transforms on `K_OP_REJECT` and `LEASE_DENY` (`adv_editor.cpp:1331-1335,1363-1384`, `adv_editor_logic.h:646-655`).
- Make `h_CanUndo`/`h_Undo`/`h_CanRedo`/`h_Redo` (`adv_editor.cpp:1177-1219`) non-destructive so native Undo/Redo still works for non-synced terrain, goal, and property edits.

### WP-7 (conditional): Findings from 4-player `=campaign` (lane 2)
- Any failures discovered once `=campaign` completes will be added as additional tickets (`BUG-1557+`) before tagging `v0.8.0.1`.
