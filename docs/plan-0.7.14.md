# SporeCoop 0.7.14 plan (draft 2026-11-13): the 0.7.14 code audit and the tickets left from 0.7.13

0.7.14 is a bug release built from a code audit the owner handed over on 2026-11-12 (75 findings, made on code close
to rel/0.7.13) and the tickets 0.7.13 left open. It starts after 0.7.13 (main efd2e0ba, tag v0.7.13). The rules of
docs/plan-0.7.11.md apply: every ticket of this plan closes in this release, fixed with a test that FAILs with its
switch off and PASSes with it, or closed with evidence after a serious attempt; "not reached" is not an outcome.

**Status: draft; owner decisions needed (end of this file).** Nothing here is implemented. Every finding was checked
by reading the code on main (no build, no lab run). Each confirmed one has a ticket with Report, Root cause (file:line
on main, observed in code or inferred), Fix idea and How to test. The first step of every package is the FAIL test.

## The audit, checked on main

| Verdict | Findings | Count |
|---|---|---|
| Confirmed (ticket opened) | SPC-1..14, 16, 19, 20 (in part), 21, 22, 23, 24 (in part); NET-1..11; KIT-1..10, 13..16; WLD-1, 3, 5, 6, 7 (in part), 8, 9, 10, 11, 13, 14 (in part), 15, 16, 18, 19, 20, 21, 23; SEAM-1 | 65 |
| Already fixed in 0.7.13 or before | WLD-2 (a: BUG-0805, b: BUG-0843, c: BUG-0842 / BUG-0805, all 570ee491); WLD-4 (the freed target and the unchecked member: BUG-0951 e0f359c9 / c93007ff, BUG-0955 06f67869; the rest, an unreset ring, is in BUG-1055) | 2 |
| Not a bug | SPC-15 (chunks of one snapshot come in order, the inbox is cleared outside active()), SPC-17 (the hooks do nothing until `coop.audio` arms them; the exe base is fixed), SPC-18 (H->rebase(0) already gives the preferred-base VA), KIT-11 (dead entries are skipped and swept at the next top-level fire), KIT-12 (the handle is left open on purpose: the queued task still signals it), WLD-12 (over 3570 cells needs cell.budget off; the game's broadphase breaks near 2048), WLD-17 (there is no g_rx_chunks; the old-cycle path is BUG-0886's twin only), WLD-22 (at most 5 fruit slots; the 32-bit read is right) | 8 |
| Duplicate of an open ticket | none | 0 |

67 tickets: BUG-0994..1060 (BUG-1003 and BUG-1041 are private security tickets in the main checkout's out/tickets/,
no text here). The 26 a player can hit are in docs/known-issues.md (the "audit 0.7.14" entries).

Where the audit was wrong in detail (the tickets say what the code does):
* SPC-1 is High, not Critical: a guest's first join into Rivals gets the host's save money as its budget by design
  (BUG-0192), the switch to Allies adds the same sum the normal path would. Money really goes wrong for a rejoiner
  (its kept budget is wiped and the save money counted instead) and, inferred, when the shared total reaches the
  loading guest's game before the deferred merge (the total doubled).
* SPC-3: hits fail in both directions, not one; a regression of the BUG-0420 friendly filter against BUG-0192.
* SPC-5: the early return is space_allies.cpp:1041; it is trigger 5 of BUG-0038, never fixed.
* SPC-12: the wave number does not grow; the same empty raid is resumed every 30 s.
* SPC-13: the window is 1.0 to about 2.3 s after the last shot, not 1.0-1.3 s.
* SPC-14: the guest sends its current values, not 0.
* SPC-16: the class is wider: civ.cpp (19 places), spacecoop.cpp (3), the adapters' g_in_call, not only 3 files.
* SPC-20: g_keys is the host's bookkeeping; the guest's effects go with its mirrors at a world change.
* SPC-22: K_SPICE (80) is safe (accepted only from origin 0); the audit missed K_BADGES_KEPT (86, new in 0.7.13).
* SPC-9: the extra add_ref is a deliberate keep-alive; the leak is that it is never released with the window.
* SPC-24: h_Relevance is right (the 12-byte check covers v[2]), coop.ui was fixed by BUG-0812, c_sample reads only
  committed stack.
* NET-2: not "any 31-byte packet": with net.halfopen_cap (default) a parseable reliable HELLO from the spoofed
  address of a live guest is needed. BUG-0778 closed only the HELLO-less case.
* NET-3: unreachable today (the largest WELCOME is about 900 bytes, the split starts at 1163).
* NET-4: 526 bytes is the format's maximum; a real reply is about 370 bytes, only a forged one is bigger.
* KIT-6: c[-7] in exe_return_address reads the PE header page (mapped, readable): harmless.
* KIT-10: the loader overflows only for a truncated path (about 248+ characters), not for a long one.
* WLD-1: the Tribe part does not apply (h_Post acts only for the avatar).
* WLD-5: g_exist_frame is a per-frame cache that clear_coop_tables resets; clear_coop_tables under g_fix_boost is
  BUG-0040's own twin switch. The real bug is Tribe's CycleTracker (BUG-1051).
* WLD-7: puppet_unregister releases the hold on every known forget path (BUG-0550); only address reuse is left.
* WLD-14: only h_KeepGround lacks the local-play check; the selection rule and the member anims are right.
* WLD-19: the NaN goes into the particles, not into c + kRot.
* WLD-22 (side note): docs/re/tribe.md contradicts itself on +0x48 / +0x4C (a u64 mask vs "all slots"); an RE
  question for the T1 agent, no code impact.
* Line numbers of the audit are from older code throughout (for_each_target is civ.cpp:5101-5117 now).

## Left from 0.7.13

| Ticket | What | Package |
|---|---|---|
| BUG-0966 | after a caught fault in a frame, outside-frame console commands get no answer | K1 (next to BUG-1024, the same family) |
| BUG-0972, 0987, 0988, 0993 | Tribe mirrors: the crouch at the fire, the thought bubble icon, the staff direction, a 1.05 s own hold | T2 (BUG-0987 may share BUG-1043's cause: the tool type 0 is dropped by fields) |
| BUG-0973 | Civ: wild creatures not synced | T2 (a feature-sized desync: scope it first) |
| BUG-0976 | Cell: cell_levelup_view fails about half its runs | C1 |
| BUG-0982 | Civ: a guest's city purchase opens the specialty question on the host | T2 |
| BUG-0989, 0990 | Creature: a joining guest inside the host's creature; the host's camera in the ground (seen once) | T2 |
| BUG-0991 | tests: the campaign finale stuck after the space bot lost its ship | S1 (the test bot) |
| BUG-0786 | fixed in 0.7.13 (personal badges); the owner's product question stays: personal badges or a shared party progress | decision 6 |

There is no BUG-0974 (the number was never used).

## Work packages (one agent, one branch, one lane pair each)

Packages are cut by the files and the shared mechanism, so parallel agents do not edit the same functions.

| Package | Mechanism | Tickets | Lanes | Twin test idea | Risk |
|---|---|---|---|---|---|
| **M1 Rivals money (first: the audit's Critical)** | The Allies merge credits a guest whose budget the host never heard (K_STATE before K_SET / K_MONEY), wipes a rejoiner's kept budget; the shared pool's floor cut not applied to the cause lines; a guest's colony spice of an older world sent as collected | BUG-1029, 1039, 1009 | 2 pairs (3 games for the rejoin case) | coop_space_rivalry: Rivals, a guest rejoins, the host switches to Allies inside the guest's load; assert both games' own money (the game's field) against the expected sum; switch `space.merge_known_budget off` | High: money in saves; decision 2 first |
| **S1 Space combat, finale, gifts** | The War partner's kPol vs the BUG-0420 friendly filter; shot counters that restart at different times on host and guest; finale "Fly to" through the paid teleport; GetPlanetRecord without in_space / generated planets; Give 10 sends to gift_target(), not the named player | BUG-1032, 1046, 1045, 1042, 1050, 0991 (test bot) | 1 pair, 1 trio (BUG-1050) | space_war_escort (the guest's ship health falls under the host escort's fire); space_give_target with 3 players; terra while the receiver is in the galaxy view | Medium: BUG-1032 needs decision 1 |
| **S2 Space rejoin window and contracts** | Gifts, sales and offers written into the temporary hold before apply_inv; the allies' pending end skipped outside Space; owed tools paid only by the contracts tick; an empty armada raid resumed forever; the 250-colony cap breaking the inner loop only | BUG-1034, 1036, 1037, 1038, 1040 | 1 pair | space_items_rejoin + a K_TOOL inside the restore window (the tool in the guest game's hold); allies end on a mirror planet, menu, new session | Low-Medium |
| **L1 Lifecycle reset (one shared mechanism)** | Per-session and per-world state that on_session_end / on_world_change / solo_start do not clear: Tribe's CycleTracker, world.cpp's pending reload, cellsync's food queue / join / own-good / body, the air holds, the parts Cell queue, mirror_fx keys, the ring, nest g_own, social queues, creature_hp puppets, drama's last events. One registry (SESSION_STATE / WORLD_STATE in the style of player_life.h, BUG-1055) with `coop.lifecycle held` listing what is still set | BUG-1055 (base), 1051, 1054, 1002, 0995 (air_release_all), 0998, 1049, 1059 | 1 pair, all stages | one general test coop_lifecycle_reset.py: every stage fixture, play, end the session (leave, drop, unload) and change world; `coop.lifecycle held` must be empty; plus the player case of BUG-1051 (rejoin to a restarted host, the guest's Tribe snapshots applied); switch `core.lifecycle_reset off` | Medium: touches every module's reset function; decision 3. Merge first in wave 1, the other packages rebase on it and leave the reset functions to it |
| **L2 Live pointer** | Raw pointers of the last tick used without the game's object list: proxy_id_of_ai over g_proxy_of, the air hold matched by address, the host running a guest's command on a cached target (tribe host_command, civ own_vehicle / resolve_target / o_SetOrder) and the read-only civ / tribe walks (for_each_target, h_Capture, h_Convert, near_own_chief); reuse game::listed / LiveRef inside game::LiveScope (BUG-0919) | BUG-1053, 0994, 0995 (the serial) | 1 pair | the "same pump" pattern of status_stale_chief: drop the target and send the command in one pump, assert the game's own state (no order on the new object at the old address); reusehunt for 0994; switch `npc.entries_listed`-style `civ.cmd_listed off` | Low-Medium: civ.cpp and tribe.cpp shared with T1 / T2 (other functions) |
| **K1 SporeKit frame and fault path** | g_present_depth not unwound by a caught SEH fault in Present (the co-op stops); the overlay's state not restored; the crash report thread reading the crash's stack after the filter returned; reports calling main-thread code; the fault logger's stack reads and lost second fault; GetModuleFileNameW results unchecked (loader and SporeKit); the triangle state block never reset | BUG-1024, 0966, 1020, 1022, 1028, 1033, 1026 | 1 lane (a long-path lane for BUG-1033, decision 5) | kit.test.excfault present (a fault inside Present caught by a handler like the game's): frames must go on (sk.frames grows, a console command answers); switch `kit.present_depth_guard off` | Medium: every frame goes through it; smoke + full before merge |
| **K2 SporeKit commands, threads, unload** | Commands flagged SK_CMD_ANY_THREAD / MAIN_THREAD that need the main thread or outside the frame (frame.limit, move, button, bg, coop.loadworld, mod.*); FreeLibrary while an any-thread command runs; notify::shutdown leaving queued tasks; ini reads without the lock; ftrace's filter string race; SK_EV_POST_INIT after a failed init; the census' lazy init | BUG-1018, 1035, 1023, 1016, 1017, 1021, 1031, 1060 | 1 lane | lab.py reload while coop.sample runs (the game survives); the rest are races: `no-fail-run` with the reasoning (decision 4) | Low: dev paths, but lab.py reload is used by every agent |
| **C1 Cell** | A mirror's NaN soft body never repaired; a chunk kill credited to the host; h_PopTiles on an empty view list and bookkeeping before the rebuild's clear; h_SpawnLoot's stack buffer | BUG-1006, 1008, 0999, 1000, 1004, 0976 | 1 pair (a trio for BUG-1006) | NaN written into one mirror's particles (the guest's game shows it back in place within 1 s); a guest jaw kill of a host NPC, the host game's killer field is the guest's puppet | Low-Medium; cellsync.cpp resets go through L1 |
| **T1 Tribe, Civ, Creature small** | The chief rescue reading max HP from +0x5D4; h_KeepGround without !local_play; the fields relay dropping 0 (tool, hunger) and PLAYER_STATE keyed by the wrong id; npc.place_now aiming one frame ahead so walk_place never snaps; the player species override lost on a null first frame; the social meter marked open before it opened; a dropped creation package never closed | BUG-1052, 1056, 1043, 1044, 0997, 1057, 1058, 0996 | 1 pair | chief_aura rescue: the saved chief's own HP field is 30 % of game::max_health; tribe tool put down, the guest's +0x102C is 0; npc_walk_player with a 5 m host teleport (the copy is there within 1 frame) | Low; BUG-0996 needs RE first (Close / Release slots) |
| **T2 Left from 0.7.13 (Tribe mirrors, Civ, Creature)** | As in their tickets | BUG-0972, 0987, 0988, 0993, 0973, 0982, 0989, 0990 | 2 pairs | their tickets' tests | Medium: BUG-0973 is feature-sized, scope it first |
| **G1 Call guard flags (class)** | Bare g_applying / g_playing / g_in_call = true / false around game calls stay true after a caught fault, the module's relay is off for the session; one registry of guard flags reset once a frame, the mechanism of BUG-0882's reset_call_guards | BUG-1047 | 1 pair | kit.test.excfault inside one guarded call (the next relay goes out) | Medium: 30+ call sites in civ, squadron, space_diplomacy, mirror_fx, spacecoop, adapters; after S1 and T1 are merged |
| **N2 Net and validation** | A seen name counted as a rejoin with the token rule on (join_closed and max_players bypassed); a spoofed HELLO dropping a live guest; a fragmented WELCOME dropped before peer_split; one oversized datagram ending the socket's reads for the frame; dbpf's flag-4 skip; pool 5 effects without the scale range; host-to-guest Space kinds accepted from a guest | BUG-1001, 1003, 1005, 1007, 1012, 1048, 1041 | none (tests/net_tests.cpp on the fake transport) and 1 trio for BUG-1001 | net_tests next to test_halfopen (FAIL with `--nofix`); coop_join_* with late join off and two guests named Player | Medium: decision 7; one direction table for all MSG_SPACE kinds (like BUG-0701's classes) instead of adding to the list |
| **P2 Diagnostics, UI, updater** | GetTickCount against QPC in net_second; fopen on a UTF-8 path; the host name cut by bytes; sreport's unreachable writing state; the updater's retry race and stale progress; apply_pack's 4 KB list; sui references never released; locked_edit_apply's UTF-8 / UTF-16 mix; debug commands trusting game state | BUG-1010, 1011, 1013, 1014, 1015, 1019, 1025, 1027, 1030 | 1 lane | chat under a caption with 30 Cyrillic letters (the game's text field has all 30); a host name of 14 CJK characters; update_check.py retry | Low |

## Order

1. Wave 1 (at most 3 lab agents): M1 (the Critical), L1 (merged first: the registry the others build on), K1.
2. Wave 2: S1, S2, C1, L2, then T1.
3. Wave 3: N2 (no lab for most), P2, K2, T2, then G1 (after S1, T1 and L2 are merged: it touches their call sites).
4. Each package starts with the tests that reproduce its findings; a finding whose test cannot be made to fail
   after a serious attempt is closed as not reproduced with the runs as evidence.
5. Integration run after every two merges; final check as in plan-0.7.13 (every new test, play_* of all stages,
   campaign_*, detect_*, the long sessions, the key-frame review, ui_golden, the same view on both games), then the
   release.

## Owner decisions needed

1. **BUG-1032 (SPC-3):** in Rivals at War, should the host's escorts and Defenders fight the guest (BUG-0192 says
   yes: the host's NPC hits go to the puppet's owner; the BUG-0420 filter turns it off now)?
2. **BUG-1029 (SPC-1):** a guest's first join into Rivals takes the host's save money as its budget (money made
   from nothing, by design of BUG-0192). Keep it, or start a newcomer at 0 or a share?
3. **BUG-1055:** a lifecycle registry in coop.cpp (an architecture change like BUG-0422) or point fixes in each
   module's reset functions?
4. **Race-only tickets** (BUG-1016, 1017, 1022, 1023, 1030, 1031 and similar): accept `no-fail-run: data race /
   hardening` with the reasoning instead of a FAIL twin?
5. **BUG-1033:** a lane with a game path of about 250 characters for the loader's FAIL test, or `no-fail-run`?
6. **BUG-0786 (left from 0.7.13):** personal badges (the 0.7.13 fix) or a shared party progress (the Discord
   suggestion)?
7. **Net:** BUG-1001: with late join off, may a player who left cleanly come back by name alone (today it can, and
   so can any new player with a seen name)? BUG-1003 and BUG-1041 are private (out/tickets/): keep them private
   until fixed and name only their numbers here? NET-3 / NET-8 (BUG-1005, 1012) are unreachable today: fix, or
   close with a guard test that fails when they become reachable?
8. **BUG-1056:** in local play (the host away), should the guest's mirrors still bypass the game's KeepGround, or
   does the game get its own check back?
9. **BUG-1050:** where does the ">" give-target picker go in Space (its place is under the jump button)?

## Owner decisions (2026-11-13)

The owner agreed to the orchestrator's proposals:

1. BUG-1032: in War the host's escort and defenders fight the guest (a war is a war); hits count both ways.
2. BUG-1029: a newcomer in Rivals starts with 0 (or the stage's starting sum), not with the host's save money.
3. BUG-1055: one shared reset registry (coop.cpp) for session end and world change, used by every module.
4. Race-only tickets (1016, 1017, 1022, 1023, 1030, 1031): closed with `no-fail-run:` and the reasoning, plus a
   stress test where one is cheap.
5. BUG-1033: closed with `no-fail-run:` (a path of about 250 characters is not worth a lane).
6. BUG-0786: badges stay personal (as 0.7.13 keeps each player's own on a rejoin) - proposed, owner to confirm.
7. Net: BUG-1003 and BUG-1041 stay private; 1005 and 1012 are closed by a test that fails once they become
   reachable. NET-1 (BUG-1001): proposed "with the token on, a rejoin only by token; without a token by name only
   when nobody of that name is connected and the name dropped recently; a name never bypasses a closed late join";
   the owner asked why a token on a LAN, answer pending.
8. BUG-1056: the agent of the Tribe package checks whether KeepGround needs a local-play bypass.
9. BUG-1050: the ">" button comes back next to "<" under "Fly to" in 3-4 player Space sessions.

0.7.14 starts only when the owner asks.
