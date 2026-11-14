# SporeCoop 0.7.15 audit: core architecture and cross-cutting machinery

Scope: mods/coop/coop.cpp, game.cpp/h, gamefixes.cpp, fixes.cpp/h, fix_switch.h, fix_table.cpp, lifecycle.h,
player_life.h, call_guard.h, busy.h, stage_epoch.h, modes.cpp/h, adapters.*, player_rules.*, fields*, hook
installation, the threading model, the fault path. Repo at v0.7.14 (a762f240), read-only; no game runs.
Numbers come from scripts over the sources, `git log`, and the frontmatter of the 1,102 tickets in docs/tickets.

## Summary verdict

The core is careful work and well documented: every decision has a reason and a ticket in the comments, the
cross-cutting registries (fix switches, call guards, lifecycle, player_life, busy gates, LiveRef) are small,
portable and unit tested, and there is almost no dead code apart from the off-paths of the fix switches. The
weak points are about structure, not local quality. (1) coop.cpp is the hub of the mod: 8.6k lines, about 300
mutable globals, and 42% of all bug tickets touched it. (2) The fix-switch system only grows: 779 switches, none
ever removed, about 930 `if` branches that keep code paths nobody runs. (3) The general mechanisms that came in
0.7.11-0.7.14 (lifecycle registry, LiveRef, call guards) sit next to the older hand-written fan-outs and
`object_alive` checks, and they have not replaced them. The two classes still open in this area (BUG-0426
object lifetime, BUG-0427 world identity) come from exactly these half-finished migrations. For 0.7.15 the best
return is to finish those migrations, retire old switches, and split coop.cpp by pure moves. A rewrite is not
needed.

## Metrics

| Metric | Value | Source |
|---|---|---|
| mods/coop size (incl. net/) | 101.7k lines at v0.7.14 (28.3k at v0.7.2.1, 59.5k at v0.7.4) | `git show <tag>:...` over tags |
| coop.cpp | 8,626 lines (3,681 at v0.7.3.0, 6,003 at v0.7.4, 8,228 at v0.7.13); 209 functions | wc, brace scan |
| coop.cpp largest functions | `sporekit_mod_main` 1,222 lines (L7404, 63 `command_add`, 39 inline lambdas); `setup_callbacks` 460 (L5019); `c_npc` 433 (L6010); `on_frame` 205 (L4147, about 80 ordered tick calls) | brace scan |
| coop.cpp content by topic (approx.) | console commands about 2,300 lines; vote / own editor about 600; session glue about 600; UI / lobby / marks about 560; world share / join about 470; diag / trace about 380; puppets about 340; DNA about 165 lines of functions plus about 25 globals | name buckets |
| other core files | game.cpp 2,692 (largest fn `init` 104); gamefixes.cpp 1,975; modes.cpp 1,748; player_rules.cpp 1,529 (`c_rules` 300 lines) | wc |
| namespace-scope mutable `g_` globals | 2,747 in mods/coop (682 of them fix bools); coop.cpp 302, tribe 260, cellsync 231, civ 230, npc 153, gamefixes 110 (27 atomics), game 91 | python scan, column 0 |
| `BUG-` references in coop.cpp | 655 (806 comment lines) | grep |
| fix switches (`COOP_FIX`) | 779 at v0.7.14. Over releases: 0 (0.7.2.4), 106 (0.7.3.0), 303 (0.7.4), 461 (0.7.10), 577 (0.7.12), 685 (0.7.13), 779 (0.7.14) | `git grep` per tag |
| switches ever removed | **0**: every name of 0.7.3.0 and of 0.7.10 is still registered | `comm` of name sets |
| lines that read a switch | about 1,505 (927 `if` lines, 167 ternaries) | python scan |
| switches by fix age | 429 for tickets fixed in 0.7.9 or earlier; 95 in 0.7.10-0.7.11; 237 in 0.7.12+; 18 unknown; 26 `COOP_FIX_UNSAFE` | ticket `fixed_in` |
| switches by area | tribe 93, core 78, npc 76, civ 67, space 61, cell 56, creature 39, creations 31, world 28, net 24 | names |
| hooks | 185 `H->hook_create` in 26 files (cellsync 31, gamefixes 22, civ 21, tribe 17, game 16, spacecoop 14, coop 9); 143 targets resolved, no target hooked by two modules | scan |
| registries | lifecycle + player_life: 36 entries in 21 files; CALL_GUARD: 18 entries, 55 holds | grep |
| liveness API | `object_alive(` 151 calls (tribe 38, npc 22, civ 15, game 13); `LiveRef` 18 mentions in 6 files; `listed(` 120 | grep |
| raw game layout | about 2,111 `skg::field<>` / `F<>` reads and about 299 `(uint8_t*)x + 0x..` in .cpp files; 37 named offset constants; `IsBadReadPtr` / `skg::readable` 506 | grep |
| message types | 54 (0.7.3.6), 59 (0.7.4), 63 (0.7.11), 67 (0.7.14) | protocol.h per tag |
| console commands registered in coop.cpp | 65 (90 in all of mods/coop, plus debug.cpp's own) | grep |
| tickets | 1,102: 975 fixed, 72 not-reproduced, 24 open; kind bug 511, desync 209, test 155, crash 118, tooling 57, feature 36, freeze 16 | frontmatter |
| bug / desync / crash / freeze tickets whose commits touched a file | coop.cpp **356**, tribe.cpp 171, npc.cpp 133, civ.cpp 126, cellsync.cpp 112, modes.cpp 85, game.cpp 83, game.h 70, gamefixes.cpp 43, player_rules.cpp 29 | ticket `commits` mapped through `git show --name-only` |
| commit churn (all time) | coop.cpp 376 commits, tribe 189, npc 168, cellsync 153, civ 122, game.cpp 94, modes.cpp 93, game.h 77 | `git log --name-only` |
| 0.7.14 alone | 31 commits touched coop.cpp, naming 53 distinct tickets | `git log v0.7.13..v0.7.14` |

Ticket classes in this area (bug-type tickets, keyword match on titles, by fixed_in):

| Class | Tickets | Trend |
|---|---|---|
| freed / stale pointer (BUG-0426, G9) | 36 | 14 in 0.7.3, then 9 in 0.7.13, 4 still open |
| session end / leave / reused id (BUG-0422, BUG-0033) | 32 | 8 in 0.7.3, then 8 in 0.7.13 and 6 in 0.7.14, 3 open |
| after a transition (editor, load, stage) (BUG-0427, G10) | 27 | mostly 0.7.3; it keeps coming back in 0.7.11-0.7.14 |
| busy states (cinematic, pause, modal) (BUG-0404, G11) | 19 | 5 in 0.7.14, 2 open |
| a caught fault leaves state behind (BUG-0110, BUG-0882, BUG-1047) | 14 | 2-3 in every release since 0.7.11 |

The umbrella tickets BUG-0421, BUG-0426 and BUG-0427 are the only class umbrellas still open. All the other
G-classes were closed in 0.7.11.

## Findings

### CORE-1 (High): fix switches are never retired; old code paths pile up

Evidence. 779 switches, and none was ever removed (the switch name sets of 0.7.3.0 and 0.7.10 are subsets of
HEAD). The count grows by 25 to 110 per release (+94 in 0.7.14). About 1,505 lines read a switch (927 `if`
lines). 429 switches belong to fixes released in 0.7.9 or earlier. The clearest cost is `end_session`
(coop.cpp:4882-4975). It still keeps two hand lists of "before BUG-0033" module calls (4903-4924) next to the
current list. Block 2 has its own `g_session_end_fix || path != Restart` condition. The keepalive keeps a whole
second `std::thread` object only for the off side (coop.cpp:3266-3269, 3345-3348, 3364-3365). Rule R3 in
fixes.h ("the off side is the code before that one fix and nothing else") cannot stay true over time. Later
fixes are written on top of the on side, so an old switch's off side becomes a combination nobody ever shipped.
Two command surfaces still point at the same bools: `coop.fix` and the legacy `coop.corefix`, `coop.sessionfix`,
`coop.pool fix` and others (docs/06-testing.md:1269-1272). 15 distinct `coop.corefix` names are still used in
tests.

Bug link. Indirect but real. Every core bug fix is written into code that has these branches (BUG-0850:
"broken if/else after BUG-0710" is a regression made inside such a branch). The pre-fix comments ("before
BUG-xxxx ...") are a large part of the 655 BUG references in coop.cpp, and they make the current behaviour
harder to read (see CORE-9).

Proposal. A retirement rule, applied in batches:
* A switch can be retired when (a) its ticket is fixed and was released at least 3 releases ago, (b) its FAIL
  run is recorded (`lab.py fixcheck` / labcov.py already checks "SPORE_FIX_OFF=x fails" claims against recorded
  runs, and the ticket `## Verification` names the run), and (c) it is not on a short keep list (switches the
  lab uses as regression canaries, and `kit.fix`).
* Retiring means: delete the off branch and the `COOP_FIX` line, keep the test as a PASS-only regression test
  (its "must FAIL with SPORE_FIX_OFF" claim goes), add a `## History` line "switch retired in 0.7.15" to the
  ticket, and rewrite the code comment to describe what the code does now.
* fixes_tests.cpp gets a check that a retired name (a list in fix_table.cpp or in the tickets) is not
  registered again and is not used by any test.
* Retire the legacy alias commands (`coop.corefix`, `coop.sessionfix`, ...) together with their tests' uses.
* First batch: core.* and session.* in coop.cpp (78 switches), including the three end_session lists and the
  keepalive old thread.

Size M (mechanical, but it touches many files: one module per commit). Risk low-medium: the danger is
deleting the wrong side of a condition. Verification: a reviewer script checks that every hunk only removes
lines or replaces `if (g_x_fix) A else B` with `A`; then build, `make test` (fixes_tests scan), and
`lab.py run smoke`. For core.*, also the session / lifecycle tests (lifecycle_reset, the leave / rejoin tests).

### CORE-2 (High): coop.cpp is the hub of almost every bug fix

Evidence. 8,626 lines, about 302 mutable globals in one anonymous namespace, 655 BUG references. 356 of the
roughly 850 bug / desync / crash / freeze tickets (42%) changed it, and it has 376 commits (twice tribe.cpp's).
It grows by 300 to 700 lines per release. It mixes at least eight concerns: the module entry and 63 command
registrations (`sporekit_mod_main` is 1,222 lines, mostly inline lambda commands such as `coop.effects`,
coop.cpp:7864-7900); the session callbacks (`setup_callbacks` 460 lines); the DNA routing (g_dna_pending,
g_dna_owed, g_dna_owed_world, g_solo_await, g_pace_heard, g_pace_carry, g_dna_spend_owed, ...); the evolution
vote and own editor (about 600 lines); world share / retry / join-apart / host-away; puppet driving (`Remote`,
drive_puppet, despawn); the lobby, marks and overlay; the diag / trace / monitor code. Test-only code is mixed
in (253 "tests" lines: the echo remote id 200, lanfake, exittest, slowdns, bodyrec, track).

Bug link. When everything is in one file with shared globals, a fix in one concern can reach the state of
another. The DNA state is reset by hand in end_session block 3, next to vote and editor state (coop.cpp:4932-
4974). The file is also the main merge-conflict point for parallel agent branches. Of the core ticket
titles: 93 are about world share / load / save, 63 session join / leave, 61 puppet body, 38 editor / evolve,
28 DNA.

Proposal. Split by pure moves (no behaviour change, one move per commit):
1. `coop_cmds.cpp`: every console command (`c_*` and the lambdas in `sporekit_mod_main`), about 2,300 lines.
   Easiest and largest win.
2. `dna.cpp`: DNA routing, its globals and their resets as SESSION_STATE entries (dna_ledger.h / dna_pace.h /
   dna_route.h already hold the portable rules).
3. `vote.cpp`: evolution vote, own editor, submit, offer, vote action.
4. `puppets.cpp`: `Remote`, g_remotes, drive_puppet, despawn, forget_puppets, monitor / bodyrec / track.
5. `session_glue.cpp`: setup_callbacks and end_session.

coop.cpp keeps `sporekit_mod_main` (init order) and `on_frame`. Shared state moves behind a small
`coop_core.h` (g_sess, g_mode, g_adapter, g_local, g_world, feed). Expected effect: coop.cpp drops below
2,000 lines, each concern's globals become file-static, and parallel branches conflict less. Size M-L (about 6k
lines moved). Risk low if the moves are exact (no edits in the same commit). Verification: build with -Wall;
`make test`; the lists `coop.fix list`, `coop.lifecycle`, `coop.players held`, and `kit` `hooks` /
command list are byte-identical before and after (dump them from one game run, or from a unit harness
that links the registries); then `lab.py run smoke`.

### CORE-3 (High): the lifecycle fan-out is done three ways at once (BUG-0427, BUG-0422)

Evidence.
* Session end: the registry (SESSION_STATE, coop.cpp:4974) runs next to 16 hand-called module
  `on_session_end()`s (coop.cpp:4888-4902, in three path lists, see CORE-1) and about 25 coop.cpp globals
  reset by hand in block 3 (4929-4973). coop.cpp itself registers 1 lifecycle entry.
* Player leave / join: `player_life::forget_all` (5446) runs next to hand calls to shared, creations, npc,
  views, pfind, cellsync (5441-5470). `g_ready.erase(id)` is done twice (5452 and 5463). The join callback
  calls `npc::on_player_leave(p.id)` by hand (5054).
* World change: at least six detectors. `avatar_change_check` (coop.cpp:3620-3640, the local pointer changed;
  it calls 6 modules by hand and is called twice per frame: 4166 behind g_frame_fix and 4286 without it),
  `tribe::world_check`, `civ::world_check`, `lifecycle_world_tick` (stage_epoch sum), `game::world_epoch`
  (BUG-0119, 13 callers), `spaceext::world_epoch` (53 callers), plus each module's own epoch checks
  (dna_spend_sample uses `game::stage_epoch`, coop.cpp:1485).
* Classes: "session end / leave / reused id" 32 tickets (14 of them in 0.7.13-0.7.14); BUG-1055 itself
  covered 7 tickets in 0.7.14; BUG-0427 (world identity) is still open.

Proposal.
1. Add observer slots to lifecycle.h: `LIFECYCLE_HOOK(name, kSessionEnd | kWorldChange | kPlayerJoin |
   kPlayerLeave, order, fn, needs_game)`. A `needs_game` hook runs only from on_frame, never inside
   `g_sess.update()` (the Disconnect path, BUG-0029). Then replace the hand lists in end_session,
   avatar_change_check and the player callbacks with registered hooks, keeping today's order through an
   explicit `order` field.
2. Move the block-3 globals of coop.cpp into SESSION_STATE entries (this comes for free with CORE-2's moves).
3. Define one world identity (`world_id = {stage_epoch sum, place}`) in game.cpp. `avatar_change_check`,
   `tribe::world_check` and `civ::world_check` become inputs to it, not separate fan-outs. This is BUG-0427's
   "one verified world-identity service".
4. Add a lint in fixes_tests style: a function named `on_session_end` / `on_world_change` /
   `on_player_leave` that is neither registered nor on an allow list fails `make test`.

Size M. Risk medium: call order matters (despawn before clear; the cellsync `cell` flag; tribe / civ decide
for themselves under their epoch fixes). Verification: a unit test in npc_tests for the order and the scopes
of the hooks; `coop.lifecycle held` after each scope; lab lifecycle_reset / lifecycle_reset_cell, the leave /
rejoin / id-reuse tests, and a 3-game "B leaves, C joins as B's id" test.

### CORE-4 (High): the liveness migration stopped halfway (BUG-0426)

Evidence. game.h:383-401 states that `object_alive` on a stored pointer reads freed memory (mbIsDestroyed of a
freed block can read 0 again: BUG-0842, BUG-0843, BUG-0844) and adds `game::listed` / `LiveRef` (0.7.13 L1).
Yet `object_alive(` still has 151 calls (tribe 38, npc 22, civ 15, game 13, worldobj 8, nest 7) and `LiveRef`
appears 18 times in 6 files. The plan (docs/generalisation-plan.md G9) asked for "lint against object_alive on
stored pointers", and no such lint exists in tools/ or tests/. Raw-pointer identity is still used in
`game::is_puppet` / `puppets()`, the creature_hp tables, and npc's proxy maps (BUG-0426 sibling list).
"Freed / stale pointer" titles: 36 tickets, 9 of them fixed in 0.7.13, 4 open (BUG-0814, BUG-0843 among them).

Proposal. (a) A make-test lint: `object_alive(` outside game.cpp needs a `// fresh: <why>` comment on the line
(a pointer taken this frame from the game's own list), otherwise it fails. Start with an allow list of today's
sites so the lint is green, then burn the list down. (b) Migrate stored pointers (struct members and globals
that hold `void*` across frames) to `LiveRef`, module by module: tribe and civ first (53 sites), then npc.
(c) `coop.live` already counts; add "object_alive on a pointer not listed" as a counter in debug builds.
Size M-L (most sites are in the stage modules, so coordinate with the stage auditors). Risk medium: `listed()`
costs one list walk per frame (already paid), and a wrong noun filter makes a live object look dead.
Verification: the lint in `make test`; lab mirror_lifetime tests (coop_tribe_mirror_lifetime,
coop_civ_mirror_lifetime, coop_npc_herd_identity, coop_tribe_chief_removed) and the chaos / free-recreate lane
of G9.

### CORE-5 (Med): the "caught fault leaves state" class has three mitigations and no lint

Evidence. The game's SEH catches faults inside mod hooks and unwinds past mod frames without running C++
destructors (call_guard.h:21-25). SporeKit only logs faults (exceptions.cpp, SK_EV_FAULT: "first chance", told
at the next frame) and does not wrap mod callbacks. Three per-frame repair registries cover it: the swap
ledger (game.h:344-368, BUG-0110 / BUG-0882), call guards (call_guard.h, BUG-1047: 18 entries, 55 holds), and
the lifecycle resets. The class has 14 tickets, 2 to 3 per release since 0.7.11. A new re-entrancy flag is
covered only if its author remembers `CALL_GUARD`. There are 191 bare `g_x = true;` lines in mods/coop/*.cpp,
and nothing checks which of them wrap an `o_*` call.

Proposal. A python lint in `make test`: a static bool or int named like a guard (`g_in_*`, `*_applying`,
`*_depth`, `g_inside*`, `t_in_*` excluded since thread_local) that is set before an `o_[A-Za-z]+\(` call in the
same function must be a registered CALL_GUARD or carry `// not a call guard: <reason>`. Also put the swap
ledger and call-guard "cleared" counts into the crash / support report (support::...), so a field report shows
that a fault left state behind. Size S. Risk low. Verification: the lint with a seeded failing example in
tools/tests; `coop.guard unwind` lab test.

### CORE-6 (Med): the threading model is implicit and its pieces are duplicated

Evidence. Everything that touches the game or the network runs on the main thread: `g_sess.update()` runs in
on_frame (coop.cpp:4176) and in `keepalive_task` through `run_outside_frame` when frames stop
(coop.cpp:3333-3349). The other threads are: the keepalive timer (it only queues), the pinned DNS thread
(coop.cpp:4980-5004), the adapters worker, autoupdate, the diag memory walk, and SporeKit's crash-report thread
(it reads `g_diag_buf` under `g_diag_mu`, BUG-1022). Hooks are also called from the game's UI skinner render
thread, and each hook body checks `fx_main_thread()` on its own (game.cpp:806-1056). The main-thread id is kept
twice: game.cpp:671 (set only in `set_effect_relay_enabled`, so it is 0 until the effects module runs, and
game.cpp:2551 has to test for 0) and debug.cpp:891. `coop.cellgone` runs as `SK_CMD_ANY_THREAD` and reads
non-atomic counters (coop.cpp:8071-8080; a benign race, but it shows there is no rule). `g_last_frame` is
defined twice (coop.cpp:3259, notify.cpp:23). docs/01-architecture.md 1.4 says only "game objects only on the
main thread".

Proposal. One `game::main_thread()` / `game::on_main()` set at the first frame in game.cpp; the effects module
and debug.cpp use it. Every hook body that the game may call from another thread starts with
`if (!game::on_main()) return orig(...)`. Add a counter of game calls made off the main thread (in the skg
helpers or a few hot wrappers) to `coop.status` / the support report. Write a "Threads" section in
01-architecture: which thread runs what, which state is shared, which locks exist (g_diag_mu, notify g_mu,
modes g_world_name_mx, effects g_qlock). Size S. Risk low. Verification: `make test`; a lab smoke in Tribe /
Civ (the skinner thread) with the counter at 0.

### CORE-7 (Med): message dispatch is a hand-written chain with silent ranges

Evidence. `g_sess.cb.custom` (coop.cpp:5108-5433) routes 67 message types with `if` chains, a `switch`, and
ranges such as `type >= MSG_UFO_STATE && type <= MSG_UFO_DAMAGE` and `MSG_CIV_STATE..MSG_CIV_BUILD`. A new id
numbered inside a range goes to that module without anyone deciding it. Anything from the host that no branch
takes falls through to `world::on_message` (5432). The world-state gate (`gate_drops`, coop.cpp:1467, through
host_world.h) is a separate list from the routing. Ticket class G3 asked for "unit test: every MSG_ type
classified"; host_world.h classifies, but routing and class are kept in two places.

Proposal. One portable table `{type, handler module, class (session / world-state / player), direction}` in
net/ or a `dispatch.h`. The callback loops over it. The unit test checks that every `MSG_` in protocol.h is in
the table exactly once and that its class equals `host_world::world_state_msg`. Keep the special cases that
come before the gate (MSG_DIAG, MSG_SETTINGS, MSG_DNA pending) as table flags. Size S-M. Risk low-medium (the
order of the checks before the gate matters). Verification: the unit test, net fuzz (fuzz_decoders), lab
smoke.

### CORE-8 (Med): the frame-order contract lives in comments

Evidence. `on_frame` (coop.cpp:4146-4351) makes about 80 calls in an order that bug fixes depend on:
"BUG-0217: before any message or module tick", "BUG-0091: forget the puppets ... before any message uses
them", "BUG-0231: the values of the last frame, as every message of this update sees them". `g_mode`,
`g_adapter`, `g_local` and `g_world` are updated after `g_sess.update()` (4176 vs 4263-4268), so message
handlers see the previous frame's mode. This is deliberate, but it is visible only in one comment. The
keepalive path runs `g_sess.update()` and `world::tick` without any of the frame phases before them.

Proposal. Split on_frame into named phase functions (`observe`, `world_change`, `receive`, `local_update`,
`module_ticks`, `send`, `drive`), each with a one-line contract. Add an "Order of a frame" table to
01-architecture, including what the keepalive path skips. Size S (pure moves inside one function). Risk low.
Verification: build, `make test`, lab smoke; the trace lines keep their order.

### CORE-9 (Med): comments tell the history, not the current behaviour

Evidence. 655 `BUG-` references in coop.cpp. Many comments describe the old code ("before BUG-0033 (tests)",
"the old way, only with the fix off", "0.7.10 called five modules by hand") or the investigation, and less
often what the code does now. Examples: coop.cpp:4847-4880 (34 comment lines before end_session) and
coop.cpp:86-139 (the `Remote` struct, where nearly every field carries a ticket story).

Proposal. Do this together with CORE-1: when a switch is retired, its comment becomes one line about the
current behaviour plus the ticket id. Keep the story in the ticket. Size: part of CORE-1. Risk none.

### CORE-10 (Low-Med): game layout is spread as magic offsets

Evidence. About 2,111 `skg::field<>` / `F<>` reads and about 299 `(uint8_t*)x + 0x..` in .cpp files, and only
37 named offset constants. The same field shows up in several modules with comments (for example cCreatureCitizen
+0x102C, cGameData +0x21 / +0x24). The .sym files with re_lint cover functions and globals, but not struct
offsets. There are 506 `IsBadReadPtr` / `skg::readable` defensive reads. They are inherently racy, and they
hide wrong pointers instead of reporting them.

Proposal. Do only the part that pays: a `game_layout.h` with named constants for offsets used in 3 or more
files, each with a link to its docs/re section, and a small lint that each such constant is mentioned in
docs/re. Do not touch single-use offsets. Size M. Risk low. Verification: build, `make test`. Since only GOG
2024 is supported, the offsets are stable, so this is about readability and finding uses, not porting.

### CORE-11 (Low): small duplication and naming issues in the core

Evidence. `avatar_change_check` runs twice per frame (coop.cpp:4166, 4286). `void* av` is computed and
discarded (4285, `(void)av;` 4329). `g_ready.erase(id)` is done twice in player_leave (5452, 5463). There are
two main-thread ids and two `g_last_frame` (CORE-6). `adapters.*` means network adapters (BUG-0286), while
`modes::Adapter` is the stage adapter, so the names collide when reading. modes.h mixes the Adapter interface
with Cell pool, Space pin and adventure helpers (modes.h:84-151). There are 54 hand-rolled "for every player
!= 0 send_*_to" loops (coop 15, tribe 13, npc 11), while `Session::broadcast_reliable` exists only for
reliable sends.

Proposal. Fix the first three in passing; add `broadcast_unreliable` and a "to guests in my world" helper,
then use them where a loop has no special filter. Leave the file names as they are (see Not worth doing).
Size S. Risk low.

### CORE-12 (Low): documentation that is out of date for this area

Evidence.
* docs/01-architecture.md 1.5 gives the symbol path as `mods/sporekit/symbols/<hash>.sym` with
  `conf high/medium/low/verified`. CLAUDE.md 1 says `data/symbols/overrides_<hash>.sym` with the words
  `verified / likely / unknown`.
* CLAUDE.md 1 names `overrides_922af7c7.sym` (GOG 2016, frozen since 0.7.8, 396 entries) as the place for
  "every game function we use", with dc04aee5 (943 entries, the only supported build) as the "too". The roles
  are reversed.
* 01-architecture has no section on threads, frame order, the fix registry, call guards, the swap ledger, or
  message dispatch; only the lifecycle registry was added (2026-11-13).
* The ticket `stage` field has 38 distinct spellings (Creature / creature, Civ / Civilization / civ,
  All / all / any, "Tribe, Civ", ...). That makes class statistics like the ones in this report unreliable.
  `tickets.py check` could normalise or refuse them.

Proposal. Update 1.5 and CLAUDE.md 1 to the dc04aee5-first layout. Add short "Threads", "Order of a frame",
"Registries" (fix / call guard / swap ledger / lifecycle / player_life / busy / LiveRef: when to use which)
and "Message dispatch" sections. Make tickets.py check enforce a stage enum. Size S. Risk none.

## Not worth doing

* **A rewrite of coop.cpp into classes, or replacing the globals with a context object across 95k lines.** It
  does not target any bug class, and it would conflict with every parallel branch for weeks. The pure moves of
  CORE-2 give most of the benefit.
* **Changing the fix registry itself.** fixes.cpp / fix_table.cpp are small, portable and unit tested, and the
  R1-R6 rules are good. The problem is retention (CORE-1), not the mechanism.
* **A generic SEH or longjmp wrapper around every hook** to stop unwinds past mod frames. MinGW GCC has no
  `__try`, and a VEH plus longjmp scheme across game frames is riskier than the per-frame repair registries
  already in place. A lint (CORE-5) is enough.
* **Moving the test commands into a separate DLL.** The lab drives release builds through them. Grouping them
  in coop_cmds.cpp (CORE-2) is enough.
* **Renaming files or namespaces** (adapters.*, modes.h) beyond comments: the churn hurts parallel branches
  more than the names hurt readers.
* **A central database of every struct offset at once**: only the offsets used in several files (CORE-10).
* **A full L2 replication rewrite in 0.7.15.** fields.h carries 7 float fields for 8 kinds today, and most
  state still travels in per-feature messages. Extending it is stage work, and it does not fit a release about
  improving what exists.

## Open questions

1. Does the owner accept a switch retirement rule (CORE-1)? CLAUDE.md 3 says the old behaviour stays behind a
   switch "so the lab can check later that the test still catches the bug". Does "later" mean forever, or until
   the FAIL run is recorded and N releases have passed?
2. How often does the lab run fix-off paths today (`lab.py fixcheck` results)? If a switch's FAIL claim has
   never been confirmed, is it retired as unverified, or verified once more first?
3. Should the frozen GOG 2016 files (gog2016_922af7c7.sym, overrides_922af7c7.sym, frozen_off_922af7c7.txt) be
   removed in 0.7.15? This is cross-area (SporeKit / build auditor). It would also simplify CLAUDE.md 1.
4. The keepalive path runs `g_sess.update()` and `world::tick` from a nested message loop with none of the
   frame's phases (no stage_epoch_tick, no lifecycle_world_tick). Is any module's message handler known to need
   those phases? If yes, this is a hidden ordering bug source (CORE-8).
5. LiveRef migration (CORE-4) touches tribe / civ / npc. Does the stage auditors' plan for 0.7.15 already
   include these sites, or should the core own the lint and the stage owners own the sites?
