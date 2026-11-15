# SporeCoop 0.7.15 audit: per-stage sync modules

Auditor: stages (tribe, cellsync, civ, npc, creations and the Creature-stage files). Repo: main at a762f240 (v0.7.14).
Read-only audit; no lab runs. Numbers come from `git log`, `ctags`, grep over `mods/coop/` and a parse of the
frontmatter of all 1102 files in `docs/tickets/`. Scripts and raw data:
`/tmp/claude-1000/-home-pam3es-Games-Modding-SPORE/13201e2f-19c5-4da6-b393-aae16fe6d338/scratchpad/audit0715/` (tk.py, cls.py, fn.py, tk.json).

## Summary verdict

* The stage modules are carefully commented and every fix sits behind a named switch with a ticket id, but they have
  turned into five separate copies of the same machinery: id <-> pointer mirror tables, chunked snapshot cycles with a
  sweep, "which world is open" watchers, host-away / held-command handling and hand-written reset lists.
* The shared pieces built in 0.7.4-0.7.14 (`game::LiveRef`, `lifecycle.h`, `fields.cpp`, `own_world.h`, `busy.h`,
  `net::HeldCommands`, `net::CycleTracker`) are good, but each one has only reached some of the sites. The old per-stage
  paths still run next to them (91 `object_alive` calls against 11 `LiveRef` uses; 3 definitions of "world changed").
* The classes that keep producing tickets are the ones these copies implement: transitions / world change (about 49
  tickets fixed in 0.7.13-0.7.14), identity and lifetime (20), host-away (25), pause (16), air / ground (18), and
  Tribe animation / hold (18). None of these is going down.
* tribe.cpp went from 2457 lines (v0.7.4) to 7811, and its churn is speeding up (about 1250 changed lines in each of
  0.7.13 and 0.7.14). It has 293 functions, 367 file-scope globals and 83 fix switches, and no unit test at all.
* What pays off most: one mirror registry with serial-based identity plus one snapshot receiver (STG-1, STG-2), one
  world-change and reset path (STG-3), one host-stand model for away, pause and editor (STG-4). Each can be done stage
  by stage behind a switch and checked with unit tests first.

## Metrics

### Size and structure (mods/coop, v0.7.14)

| File | Lines | v0.7.4 lines | Functions | Largest function | Distinct `g_*` names | COOP_FIX | Commits | Commits naming a BUG | Distinct BUG ids in file |
|---|---|---|---|---|---|---|---|---|---|
| tribe.cpp | 7811 | 2457 | 293 | debug 1356 (6320); on_member_anims 282; guest_state 189 | 334 | 83 | 219 | 184 | 139 |
| cellsync.cpp | 7331 | 5019 | 254 | debug 1743 (5355); on_message 256; pop_tiles 166 | 356 | 61 | 178 | 137 | 94 |
| civ.cpp | 6647 | 3310 | 230 | debug 983 (5569); host_tick 235; host_command 196 | 310 | 62 | 144 | 108 | 83 |
| npc.cpp | 4258 | 2668 | 160 | guest_tick 337 (2171); on_message 270; host_tick 246 | 187 | 59 | 195 | 137 | 105 |
| creations.cpp | 3756 | 2458 | 147 | debug 410 (3322); apply 120 | 168 | 34 | 71 | 49 | 44 |
| effects.cpp | 1277 | - | 46 | on_message 146 | 104 | 11 | 26 | 26 | - |
| nest.cpp | 981 | - | 43 | c_nest 152 | 29 | 6 | 11 | 9 | - |
| creature_hp.cpp | 724 | - | 31 | c_hp 190 | 26 | 6 | 17 | 15 | - |
| parts / realign / mirror_fx / drama / census / audio | 636 / 511 / 374 / 332 / 240 / 213 | - | - | <= 95 | - | 5/4/2/5/1/0 | 4/5/7/10/15/3 | - | - |

* The stage files hold 339 of the 753 `COOP_FIX` switches in the repo (45 %). 151 of them belong to tickets below
  BUG-0500, which were fixed by 0.7.4 at the latest.
* The four `debug()` console functions add up to 4492 lines, 18 % of tribe + cellsync + civ + creations. They are
  test and diagnostic code built into the shipped DLL.
* npc.cpp `guest_tick` (337 lines) reads 24 different fix flags, and tribe.cpp `on_member_anims` reads 12.
* Containers keyed by a raw game pointer (`map/set<void*>`): tribe 32, civ 26, npc 13, creature_hp 2 (squadron 4).
* Raw struct-offset reads (`F<T>(p, 0x..)`): cellsync 404 (87 distinct offsets), tribe 164 (72), nest 46, civ 39.
  The same offset shows up in several files (for example 0x120 combatant in 8 files, 0x10F0 in 5).
* Liveness checks in the stage files: `game::object_alive` (memory only, which game.h:386-389 itself calls unreliable)
  91 calls (tribe 38, npc 22, civ 15, nest 7, drama 3, creature_hp 3, others 3). `game::alive` / `LiveRef`: 11 calls.
* Unit tests: cell (cell_logic.h, 104 checks), npc / drive (npc_tests.cpp, 157 checks) and space (space_logic.h, 37
  checks) have portable rule headers. Tribe and Civ (14.4k lines, 236 defect tickets) have none.

### Churn per release (lines added + removed)

| Release | tribe | cellsync | civ | npc | creations |
|---|---|---|---|---|---|
| 0.7.4 | 59 | 232 | 622 | 445 | 780 |
| 0.7.5 | 669 | 53 | 562 | 0 | 133 |
| 0.7.6 | 305 | 0 | 756 | 0 | 0 |
| 0.7.7 | 267 | 25 | 0 | 70 | 424 |
| 0.7.8 | 364 | 0 | 1 | 122 | 0 |
| 0.7.9 | 460 | 598 | 268 | 382 | 0 |
| 0.7.10 | 638 | 206 | 268 | 63 | 253 |
| 0.7.11 | 173 | 468 | 248 | 162 | 274 |
| 0.7.12 | 473 | 165 | 414 | 188 | 62 |
| 0.7.13 | 1258 | 404 | 801 | 413 | 164 |
| 0.7.14 | 1219 | 805 | 587 | 434 | 128 |

### Tickets

1102 tickets in total: 975 fixed, 72 not-reproduced, 24 open (the open ones are mostly umbrellas and feature ideas).
Defect tickets (kind bug / desync / crash / freeze) per stage: Creature 212, Tribe 155, Space 112, Cell 101, Civ 81,
All 166.

Bug classes. These are keyword matches over title plus the first 2.5k characters, so treat them as rough. One ticket
can fall into more than one class. The numbers are defect tickets only.

| Class | Total | Cell | Creature | Tribe | Civ | Space | All | Fixed in 0.7.3 | 0.7.4-0.7.12 | 0.7.13 | 0.7.14 | open / none |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Transition / load / stage change / world change | 166 | 17 | 49 | 44 | 16 | 14 | 26 | 46 | 50 | 24 | 25 | 18 |
| Editor round trip | 143 | 16 | 53 | 21 | 9 | 14 | 30 | 42 | 52 | 17 | 9 | 19 |
| Placement / catch-up (BUG-0425) | 103 | 10 | 35 | 30 | 11 | 8 | 9 | 24 | 50 | 6 | 10 | 13 |
| Rejoin / late join / new host | 96 | 7 | 20 | 20 | 7 | 22 | 20 | 25 | 37 | 14 | 8 | 12 |
| Host away / partner absent | 95 | 7 | 24 | 20 | 11 | 18 | 15 | 31 | 33 | 14 | 11 | 6 |
| Air / ground height | 75 | 5 | 31 | 12 | 11 | 5 | 11 | 17 | 30 | 9 | 9 | 10 |
| Identity / lifetime (BUG-0426) | 68 | 2 | 25 | 13 | 7 | 10 | 11 | 21 | 19 | 12 | 8 | 8 |
| Pause / game time stands | 66 | 4 | 17 | 18 | 10 | 6 | 11 | 21 | 25 | 8 | 8 | 4 |
| Sent but not applied (BUG-0419) | 60 | 3 | 15 | 6 | 6 | 6 | 24 | 13 | 24 | 11 | 7 | 5 |
| Tribe animation / hold / tool (title match) | 32 | - | - | 32 | - | - | - | 2 | 10 | 10 | 8 | 2 |

Reopened tickets (the History says "reopen"): 10, 6 of them Tribe (BUG-0276, 0410, 0440, 0528, 0531, 0587). The more
usual pattern is not a reopen but a sibling: a new ticket for the same class at another site or in another stage.
Examples: BUG-0844 (Tribe), BUG-0951 (Tribe and Civ) and BUG-1053 (Tribe and Civ) for the identity class;
BUG-0960 -> 1081 -> 1086 -> 1101 for the Tribe pause and caption wait; BUG-1089 -> 1100 for Civ pooled NPCs. The
umbrellas BUG-0421, BUG-0426 and BUG-0427 are still open.

## Findings

### STG-1 (high): identity of game objects is re-implemented per stage; one mirror registry should own it

Evidence:
* There are 10 hand-maintained bidirectional id <-> pointer maps, two per stage (host and guest):
  npc.cpp:101-102 `g_host_npcs` / `g_host_by_id` and 304, 343 `g_proxies` / `g_proxy_of`; tribe.cpp:303-304 `g_hm` /
  `g_hm_by_id` and 357, 429 `g_gm` / `g_gid_of`; civ.cpp:390-391 `g_hv` / `g_hv_by_id` and 456-457 `g_gv` / `g_gv_of`;
  cellsync.cpp:143-144 `g_mirrors` / `g_host_of`; squadron.cpp:78-79, 97-98.
* Each stage has its own defence against address reuse:
  * npc host: species plus dead flag (`npc_object_reused`, npc_ids.h:30);
  * npc proxies: serial (npc.cpp:2022 `px_alive`, BUG-0842);
  * civ: political id, then the serial added on top (civ.cpp:356-357, BUG-0059 then BUG-0951);
  * tribe: serial through `kept_alive` (tribe.cpp:204) and `hm_live` (3284);
  * cell: pool-index generation.
* Keeping the two directions in step is itself a bug site: civ.cpp:4608 `if (g_gv_of[...] == it->first) // (BUG-0951:
  not another mirror's)`.
* 91 `object_alive` calls remain, although game.h:386-389 says its memory "says nothing" about a freed object. The
  general fix that umbrella BUG-0426 asks for ("identity = pointer + serial everywhere ... lint against object_alive on
  stored pointers; one registry of mod-made objects for savestrip and destroy") is only partly done.

Bug link: about 68 identity / lifetime defect tickets, 20 of them fixed in 0.7.13-0.7.14: BUG-0842, 0843, 0844, 0951,
1053 (the same fix in Tribe and Civ, tribe.cpp:3280 and civ.cpp:396), and BUG-1100 (found by the 0.7.14 final check:
the species heuristic missed a pooled reuse during a pause).

Proposal: a portable `mirror_registry.h` (no game calls; the liveness function is injected), templated on the id type:

* `Entry{id, ptr, serial, noun, owner}`;
* `bind(id, ptr, serial)` and `unbind(id)`, which keep both maps consistent and refuse a pointer that is bound to
  another id;
* `get_live(id)` and `id_of_live(ptr)` through `game::alive`;
* `for_each_live` and `sweep(seen_ids, hold_pred, on_remove)`;
* `held()` for lifecycle.h, and a `savestrip` filter for free.

Migration order: Tribe guest mirrors and Civ guest vehicles first (the largest and most recent sources), then the host
tables (`g_hm`, `g_hv`, `g_host_npcs`), then npc proxies. Each move goes behind one switch per stage. Add a lint
(re_lint style, in `make test`) that fails on a new `object_alive(` in the stage files unless the line is on an
allowlist.

Size: L in total (M per stage). Risk: medium. The binding rules differ in their details (Tribe binds by position on
the first cycle, Civ holds removals while the planner is open), so the registry needs a hold predicate and a "bind
candidate" callback, not a single policy. Verification: unit tests for the registry (rebind of the same address with
another serial, two ids for one pointer, an incomplete cycle that removes nothing, a held removal). Lab:
tribe_mirror_*, civ_wild_reused / civ_wild_pooled, the chaos lanes, and the detector `world_diff` gate. Each migrated
stage gets its existing class tests run once with the switch on and once with it off.

### STG-2 (high): the chunked snapshot cycle and its sweep are copied in tribe, civ and squadron

Evidence: tribe.cpp:4305-4490 and civ.cpp:4486-4615 run line for line the same steps:
* `cycle_chunk_ok`, `g_cyc.begin(cycle, g_cycle_order_fix, &started)`, `if (started) g_cycle_ids.clear()`;
* after the chunk: `if (!r.ok()) return; g_cyc.add(chunk); if (chunk + 1 < chunks) return; if (!g_cyc.complete(chunks)) { g_incomplete++; return; }`;
* then a sweep of the mirror table that are not in `g_cycle_ids`.

The same state is declared twice with identical comments (tribe.cpp:430-450, civ.cpp:481-493, including the
LIFECYCLE_STATE entry). squadron.cpp:924-1028 has a third copy. Bug link: BUG-0612 (cycle order), BUG-1051 (cycle
reset), BUG-0685 / BUG-0951 (sweep removing another object): each was fixed in two or three places.

Proposal: `net::SnapshotReceiver<Id>` (CycleTracker plus seen ids plus `on_complete(sweep)`) in net/validate.h, used
together with STG-1's `sweep`. This takes about 60 lines out of each stage and turns the "a cut chunk removes nothing"
rule into one unit test.

Size: S-M. Risk: low (portable code, already covered by net_tests for CycleTracker). Verification: net_tests (lost
chunk, older cycle, restart); lab sync_burst / tribe and civ loss tests.

### STG-3 (high): three definitions of "the world changed" and hand-written reset lists next to the registry

Evidence:
* (a) coop.cpp:3624-3640 `avatar_change_check`: the local entity pointer changed, including an editor visit. It calls
  `npc`, `nest`, `fields` and `wobj` `on_world_change`.
* (b) tribe.cpp:6121-6146 and civ.cpp:5249-5272 `world_check()`: a byte-for-byte copy (stage epoch plus in-stage
  signals, `g_world_left`, `g_world_checks_fired`), added for BUG-0217 / BUG-0223 because (a) dropped state on editor
  visits.
* (c) lifecycle.h `WORLD_STATE`: the sum of the stage counters moved (docs/01-architecture.md#arch-lifecycle-registry).
* tribe.cpp:6050-6105 clears the same 12 tables by hand in both `on_session_end` and `on_world_change`, and civ does
  the same (civ.cpp:5169-5235). The lifecycle registry carries only 4-5 entries per module.
* `end_session` calls the module list three times with different members (coop.cpp:4887-4925, the old paths of
  BUG-0033).

So a table can be reset by (a), (b) or (c) on different frames, and a new table has to be added to two hand lists and
picked under one of three triggers.

Bug link: the transition / world-change class is the largest (166 defect tickets) and has not shrunk: 24 fixed in
0.7.13, 25 in 0.7.14, 18 open or without a version. The umbrella BUG-0427 is open. Siblings: BUG-0059, 0217, 0223,
0672, 1051, 1054, 1055.

Proposal:
1. One `stage_world::Watch` helper (in stage_epoch.h, portable) that replaces both `world_check` copies.
2. Move every plain clear of the tribe and civ hand lists into `LIFECYCLE_STATE` entries, so `on_session_end` and
   `on_world_change` keep only the game calls (restore chief scales, destroy the epic).
3. Decide for the npc, nest, fields and wobj group whether an editor visit is a world change. Today it is, through (a),
   unlike Tribe and Civ. Write the answer down and route it through the same watch.
4. One ordered module table for `end_session`, instead of three hand lists.

Size: M. Risk: medium. The comments at tribe.cpp:6080 and civ.cpp:5221 warn that the order of the restoring clear
matters (BUG-0217 / BUG-0223), so the game-call part must stay in the modules. Verification: unit test of the watch
rule (editor visit, reload of the same save, stage change); `coop.lifecycle held` in the existing
coop_lifecycle_reset and wsync_transitions lab tests; editor round-trip tests per stage.

### STG-4 (high): host away, host pause and host-in-editor are handled differently in every stage

Evidence:
* Away: tribe.cpp:66-72 and civ.cpp:75-81 are the same block (`g_fix_away`, `g_away_orders`, `local_play()`).
  npc.cpp:402 / 2713-2725 keeps its own shadow `g_host_away` with transition code. cellsync.cpp:4530 plays its own pool
  on `host_away || elsewhere() || host_left()`. Tribe and Civ ignore `elsewhere` and `host_left`. nest.cpp:531,
  realign.cpp:401 and worldobj.cpp:1474-1518 each read `C.host_away` themselves.
* Held commands: the wiring is copied twice (tribe.cpp:5829-5842 / 5950-5960 and civ.cpp:1427-1440 / 4818-4827,
  including the `active_at` / `active_since` settle logic). The copies already differ: Tribe drains only in kTribe mode
  and Civ has no mode check; Civ tells the guest that its order waits (`say_host_away`, civ.cpp:1442) and Tribe does
  not, except for conquests.
* Pause: the host's pause reaches the guests only in Tribe (BUG-1086, 0.7.14), tunnelled as MSG_NPC_ANIM kind 21
  (tribe.cpp:2526-2680). Tribe also has its own safety net for a pause the game leaves standing (BUG-1101,
  tribe.cpp:2689-2786). No other module calls `world::time_pause`. docs/03-gameplay.md section 3.3 says "Pause | host:
  pauses for everyone", so the docs and the code disagree for Cell, Creature and Civ.
* busy.h already defines the host's stand (cinematic, input lock, paused, editor, loading) as a portable, unit-tested
  rule, but only for the local game.

Bug link: host-away 95 tickets (25 in 0.7.13-0.7.14: BUG-0879, 1056, 1084 ...); pause 66 (16 in 0.7.13-0.7.14:
BUG-0927, 0960, 0982, 0983, 1081, 1086, 1100, 1101).

Proposal:
1. The host sends its stand (busy bits plus away plus its stage and world) as one small reliable state: a wsync row or
   a field of the player state. Guests get one `peer_stand()` API.
2. Each stage declares a policy row {while the host stands: wait with a note | play on locally | hold commands; on
   resume: settle time}.
3. One `held_cmds_pump(stage, mode, handler, say)` replaces the two copies.
4. The Tribe kinds 20 / 21 go onto it, with an old-guest fallback.

Whether Cell, Creature and Civ guests should wait while the host is paused is an owner decision (see Open questions).

Size: M. Risk: medium (protocol compatibility with 0.7.14 guests: keep sending kind 20 / 21 for one version).
Verification: unit tests for the policy and the pump (settle, world mismatch, a leaver's commands); lab: the existing
Tribe pause tests plus one new class test per stage (host pause menu 30 s, guest acts, host resumes, compare world and
orders).

### STG-5 (medium): MSG_NPC_ANIM carries about 15 Tribe sub-kinds through npc.cpp

Evidence:
* npc.cpp:3000 whitelists `kind 2..12, 14, 15, 16, 20, 21` and forwards them to `tribe::on_member_anims`.
* tribe.cpp sends both reliable and unreliable kinds on the same type (1988-1990, 2301-2303): member animations, the
  theme music, the party music, member air height, the game-over wait and the host pause.
* The world gate drops every MSG_NPC_ANIM while the guest is not in Tribe, which needed a special case for the party
  stop (BUG-0890, tribe.cpp:2414-2419).
* Every new kind needs an edit in npc.cpp's whitelist, and an old guest ignores it silently (the comment at
  tribe.cpp:2553).

Proposal: Tribe gets its own message type with a dispatch table owned by tribe.cpp, and the waits move to STG-4. The
gate then decides per kind (stop and game-over in any mode).

Size: S-M. Risk: low-medium (protocol: version-gate the switch). Verification: fuzz_decoders and net_tests for the new
decoder; lab tribe_mirror_anims, party and theme music tests.

### STG-6 (medium): Tribe animation and hold mirroring is a pile of event special cases

Evidence: about 20 switches in tribe.cpp:1022-1144 and 1766-1935: mirror_hold, mirror_hold_only, hold_item, hold_anim,
hold_queued, hold_cut_frame, mirror_anims, mirror_idle, mirror_stale_anim, mirror_idle_loco, mirror_phase,
mirror_phase_any, mirror_phase_tracks, hold_restart. Each one adds a message kind or a timing window
(kHoldMax 1.5, kHoldBoundMax 8.0, kHoldUnsent 0.35, kMirrorAirFresh 0.35 ...). `on_member_anims` is 282 lines.

Bug link: 32 Tribe animation / hold / tool tickets, 18 of them fixed in 0.7.13-0.7.14 (BUG-0971, 0972, 0988, 0992,
1043, 1069, 1071, 1072 ...), two still open (BUG-0987). This is cause E of docs/sync-architecture-plan.md ("events
instead of state").

Proposal: replicate a per-member animation state record (main animation id plus time, other tracks, hold handle and
item, idle) with apply-on-difference, the same model as fields.cpp. The guest converges to the state instead of
replaying start and stop events. Start with a spike that measures how often the record differs on same_view_tribe.

Size: L. Risk: high (visual tuning, many hand-tuned windows). Do it after STG-1 and STG-2, or in 0.7.16 with an owner
go-ahead. Verification: the same_view_tribe key frames and the golden views (visual checks), tribe_mirror_* lab tests.

### STG-7 (medium): split the huge files along existing seams, starting with the debug commands

Evidence: the four `debug()` functions (cellsync.cpp:5355 1743 lines, tribe.cpp:6320 1356, civ.cpp:5569 983,
creations.cpp:3322 410). tribe.cpp has clear topic blocks:
* the body and member table (180-500);
* orders and the ring (690-900);
* held items and tools (960-1300);
* animations and music (1760-2440);
* the game-over and pause wait (2526-2790);
* babies (3340-3420);
* mirrors and the snapshot (3690-4500);
* epics and chiefs (4777-5420);
* conquest and captions (5660-5700);
* held commands and world (5829-6146).

The high commit counts (219 on tribe.cpp, 195 on npc.cpp) mean the parallel branches of each wave keep touching the
same file. That is where merge conflicts and union-merge cleanups come from.

Proposal: first move `debug()` into `<stage>_debug.cpp`. This needs a `<stage>_internal.h` that declares the state the
command reads; group it into a few structs (GuestMirrors, HostTable, Counters) instead of exporting 300 statics. Then
split tribe.cpp into 4-5 files along the blocks above, and civ.cpp into vehicles, cities and buildings, space builds,
and shots. Do no logic changes in the same commits.

Size: M. Risk: low if the moves are pure (a build, make test, one smoke). Verification: build, `make test`, the
`coop.<stage>` output diffed before and after on one lab smoke run. Do this together with STG-1 (the registry is the
natural home of the mirror state) and not as a separate pass, or it doubles the churn.

### STG-8 (medium): Tribe and Civ rules have no unit tests; extract them into portable headers

Evidence: Cell (cell_logic.h, 104 checks), npc / drive (npc_ids.h, drive_plan.h, realign_rule.h, 157 checks) and Space
(space_logic.h) have portable rule headers. Tribe and Civ have none, although their rules are pure decisions: the
game-over / pause reason (tribe.cpp:2565-2680 `gameover_why`, `gameover_host_tick`), the pause net
(`pause_net_tick` 2745-2786), `local_play`, the held-command settle, the hold windows, the conquest-while-away rule,
the vehicle sweep with holds. The owner's lab time budget (lab tests take 90 % of the time) makes unit tests the cheap
way to keep these from regressing.

Proposal: `tribe_logic.h` and `civ_logic.h` with the decisions as functions of plain inputs, and tests in a new
tests/tribe_tests.cpp. Do them one by one, each in the change that touches the rule anyway.

Size: S per rule. Risk: low. Verification: `make test`; the rule tests must fail on the pre-fix behaviour, as
CLAUDE.md 3 asks.

### STG-9 (medium): quick win: stop new memory-only liveness checks now

Evidence: as in STG-1, 91 `object_alive` calls and 73 pointer-keyed containers in the stage files. The comment at
game.h:386-389 and three 0.7.13 tickets (BUG-0842, 0843, 0844) show that `object_alive` on a kept pointer is wrong.

Proposal: before STG-1 lands, a lint in `make test` that counts `object_alive(` per stage file and fails when the
count grows (a ratchet), plus a one-line comment rule in CLAUDE.md 2. Convert the obvious kept-pointer sites (tribe
`g_detached`, nest egg mirrors, drama flock) to `LiveRef` as each is touched.

Size: S. Risk: none. Verification: make test.

### STG-10 (low-medium): old fix switches and per-module fix toggles make the hot paths harder to read

Evidence:
* cellsync.cpp:5357-5378 (`coop.pool fix <name>` with its own table of 10 flags), tribe.cpp:6337 (`fix
  chiefs|gameover`) and civ.cpp:5607 (`fix boost|world`) duplicate `coop.fix`.
* 151 of 339 stage switches are older than 0.7.4.
* npc.cpp `guest_tick` branches on 24 distinct flags, so the "off" combinations are untested.

Proposal (coordinate with the core / fix-switch auditor): delete the per-module toggles in favour of `coop.fix`.
Retire switches whose ticket was released at least N versions ago and whose test still passes, keeping the twin only
where a lab test still uses it to prove that it catches the bug (CLAUDE.md 3).

Size: S-M. Risk: low (lab scripts that call `coop.pool fix` or `coop.tribe fix` need an update: grep tools/tests).
Verification: grep tools/tests for the old commands; smoke.

### STG-11 (low): the ground and height rule is shared for creatures but re-done for eggs and absent for vehicles

Evidence: driver.h:150-185 is the one height rule for remote bodies (BUG-0425: on its ground, or the sender's height
over its ground). nest.cpp:38 has its own `over_ground`. civ.cpp has its own away ground check (BUG-1056, civ.cpp:512-525)
and never calls `ground_radius`. Air / ground tickets: 75, 18 of them in 0.7.13-0.7.14 (the hang in the air of BUG-1082
and BUG-1101, civ vehicles under ground in BUG-1056).

Proposal: route egg and pickup placement through `drive::` (onto_ground / at_sender_agl), and write down the Civ
vehicle height rule in docs/re/civ.md before deciding whether vehicles share it.

Size: S. Risk: low. Verification: the detector's height fields, nest and civ ground lab tests.

### STG-12 (low): game struct offsets are spread inline across files

Evidence: 404 raw offset reads in cellsync, 164 in tribe. The same offsets appear in several files (0x120, 0x10F0,
0x158), with names only in comments (tribe.cpp:4843 `F<void*>(t, 0x368) // cTribe::mpHut`; wsync.cpp reads the same
field).

Proposal: named accessors (`tribe_obj::hut(t)`) only for offsets used in more than one file, next to docs/re.

Size: S-M. Risk: low. Value: moderate. It also makes a future build port (new exe) a header edit. Verification: build,
make test.

## Not worth doing

* A generic replication engine for every stage in one release (the full L2 of docs/sync-architecture-plan.md).
  0.7.4-0.7.14 showed that the engines (fields, wsync, worldobj) work when they grow one kind at a time. A big-bang
  rewrite of five stages would break the lab's fail-before / pass-after evidence chain.
* Folding cellsync into the shared mirror layer. Cells are pool slots with a generation, in a 2D world with their own
  physics. Their identity works (2 identity tickets in Cell), and cellsync's churn is about food, KO and editor rather
  than identity.
* Moving Civ vehicles or Space ships onto driver.h. Their locomotion is not creature locomotion, and the gain is
  unclear (keep them behind STG-1's registry only).
* Reorganising creations.cpp. Its churn is low and falling (62 / 164 / 128 lines in 0.7.12-0.7.14), and its 410-line
  debug is the only big block.
* Renaming every hex offset or literal timing (about 2300 hex literals, about 900 float literals). Most timings are
  already named `k*` constants with a ticket. Name only what is shared (STG-12).
* Removing every fix switch. The twin switches are how the lab proves that a test catches its bug. Retire them by
  age and evidence (STG-10), not wholesale.
* Splitting files without consolidating state (STG-7 alone). That only moves 300 statics into headers and doubles the
  merge churn.

## Open questions

1. Does the game's herd pool (BUG-1089 / BUG-1100) hand out a reused creature through `CreateInstance`, so that its
   serial (`cGameData::mID`) changes? If yes, the npc host table can drop the species-plus-dead heuristic
   (npc_ids.h:30) for serial identity, like the other stages. docs/re/space.md#space-object-serial also leaves open
   whether a load resets the counter.
2. Owner decision: should Cell, Creature and Civ guests wait while the host is paused, as Tribe does since BUG-1086?
   docs/03-gameplay.md 3.3 says "host: pauses for everyone", and the code does that only in Tribe.
3. Should an editor visit count as a world change for npc, nest, fields and wobj, as coop.cpp:3624 does today? Tribe
   and Civ explicitly stopped treating it as one (BUG-0217 / BUG-0223).
4. Are the Tribe member mirrors (tribe.cpp `g_gm`) and the npc proxies for Tribe animals meant to stay two systems?
   Both drive creatures with driver.h, with different delays and grounding (kMirrorDelay 0.2 at tribe.cpp:405,
   npc kStateInterval 0.1).
5. Which of the 151 pre-0.7.4 switches in the stage files does a lab test still flip? That decides what STG-10 may
   delete (tools/tests grep plus the lab's fix-off runs).
