# SporeCoop 0.7.15 plan (draft 2026-11-14): a quality release

0.7.15 improves what exists: fewer bugs from better structure, a faster and less noisy lab, a safer release path,
and the project-health work any project needs. It adds no gameplay feature. It starts after 0.7.14 (main a762f240,
tag v0.7.14).

**Status: draft; owner decisions needed (end of this file).** Nothing is implemented. The plan comes from five
read-only audits of main a762f240 (2026-11-14), each with evidence (file:line, counts, ticket history, lab history):

| Area | Report | Findings |
|---|---|---|
| Core: coop.cpp, fix switches, lifecycle, threads, dispatch | docs/audit-0.7.15/core.md | CORE-1..12 |
| Stage modules: Tribe, Civ, Cell, NPC, Creature, creations | docs/audit-0.7.15/stages.md | STG-1..12 |
| Net, protocol, Space, pools | main checkout out/audit-0.7.15/net_space.md (not in git: security pointers) | NET-1..12, SEC-1..3 |
| SporeKit, build, CI, release, docs, repo | docs/audit-0.7.15/kit_build.md | KIT-1..16 |
| Tests and lab | docs/audit-0.7.15/tests.md | TST-1..9 |

The ticket counts in the reports come from keyword matching over ~1100 tickets: read them as sizes of a class, not
exact numbers.

## Verdict

The code is careful and well documented. Every fix has a ticket, a switch and a test, and the portable parts
(net, fixes, call_guard, lifecycle, LiveRef, SporeKit) are unit tested, fuzzed and clean. There is almost no dead code.
The bugs do not come from sloppy code. They come from structure:

1. **The same machinery is written five times.** Each stage keeps its own id <-> pointer mirror tables, its own
   "delete what the host's snapshot no longer has", its own "the world changed" check, its own host-away / pause /
   editor handling and its own reset lists (STG-1..4, CORE-3). The classes behind them are the biggest and they do
   not shrink: "world changed / session reset" about 166 tickets (24 fixed in 0.7.13, 25 in 0.7.14), "reused or
   freed game object" about 68 + 36.
2. **Migrations stopped halfway.** The shared mechanisms of 0.7.13-0.7.14 (lifecycle registry, LiveRef, busy,
   HeldCommands) run next to the old hand paths instead of replacing them: `object_alive` (reads freed memory) has
   151 calls, LiveRef 18 (CORE-4); lifecycle fan-out happens three ways (CORE-3); Space has its own reset path and
   four kept-state protocols (NET-1).
3. **Nothing is ever removed.** 779 fix switches, none retired; ~1500 lines read a switch, ~930 `if` branches keep
   both paths alive; 429 switches are for fixes from 0.7.9 or older (CORE-1).
4. **A few files are hubs.** coop.cpp (8.6k lines, ~300 globals) was touched by 42% of all bug tickets; tribe.cpp grew
   from 2.5k (0.7.4) to 7.8k lines with 367 globals, 83 switches and no unit tests (CORE-2, STG-7).
5. **The lab costs more than it finds.** 28% of all lab time went into failed results; in the 0.7.14 final check
   about 2 of 13 failures were mod bugs. The full campaign is 23% of lab time and fails 38 of 63 runs, a third of
   them with no reason printed (TST-1..3, TST-6, TST-8).
6. **The release path breaks late.** 3 of the last 9 tags failed only in CI (KIT-1).

A big-bang rewrite is not the answer (every report says so). The plan finishes the migrations one stage at a time,
behind switches, unit tests first, and deletes the old paths as it goes.

## Rules for this release

* The rules of docs/plan-0.7.11.md apply: every ticket of this plan closes in this release; a behaviour change has a
  FAIL/PASS twin (switch off / on), or for refactoring the proofs below.
* **Pure moves** (a file split, a function moved) carry no edits in the same commit; proof: the build, `make test`,
  and identical dumps of `coop.fix list`, `coop.lifecycle`, the hook list and the message table before and after.
* **A shared mechanism replaces a stage path behind a switch**; the stage's existing lab tests must pass with it on,
  and the old path is deleted in the same release once they do (no new permanent switch).
* **Unit first:** the logic of a new shared mechanism is a portable header with unit tests before any stage uses it.
* Wire bytes stay identical unless the package says so (golden-byte tests); 0.7.14 and 0.7.15 must play together.
* Every package ends with its lab tests only; the final check runs once at the end on 4 lanes (VRAM, BUG-1098).

## Packages

Order: wave A first (small, independent, cuts lab time and noise for everything after), then B (foundations the big
refactors stand on), then C (the shared mechanisms, one stage at a time), then D (tests on the new structure).

### Wave A: quick wins (each S)

| Id | What | From |
|---|---|---|
| A1 | Privacy: mask a non-ASCII Windows user name in logs and reports (USERPROFILE via the W API, unit test) | KIT-7 |
| A2 | Release path: `make release-dry` from a `git archive` copy with CI's Python 3.10 and toolchain; tag only on a green main; immutable releases | KIT-1 |
| A3 | CI supply chain: actions pinned by SHA, llvm-mingw tarball checked by SHA-256 | KIT-12 |
| A4 | Compiler: `-fno-strict-aliasing -fno-delete-null-pointer-checks`; format attributes on SkHost::log / out_printf; fix the 21 warnings; `-Werror` | KIT-4, KIT-5 |
| A5 | Stale GOG 2016 texts: setup window, zip README, INSTALL.md, CLAUDE.md symbol file (the code itself: owner question 3) | KIT-8, CORE-12 |
| A6 | Campaign reporting: `[stuck]` / `[desync]` print FAIL lines with a reason; `--jump` on so one stuck stage does not end the run; a final-check rerun runs only the failed segment | TST-1 |
| A7 | One flaky ledger: `lab.py flaky` over every results.json (all worktrees), fix-off twins excluded (results.json gets a fix_off field) | TST-2 |
| A8 | `--repeat` stops when the verdict is clear, prints the rate, fail-fast mode | TST-8 |
| A9 | Start-up: the fixed 4 s / 3 s sleeps of cold start and warm reset become condition waits | TST-6 |
| A10 | NET-2 (public ticket): kept Space state keyed by the rejoin token, not the display name (two "Player"s swap colonies) | NET-2 |
| A11 | SEC-1..3 (private tickets): per-peer queued-bytes cap, relay budget, `coop.kick`; K_MONEY range check and no int overflow in the Allies merge | NET-5, NET-11 |
| A12 | Tickets: `stage` field validated (38 spellings today) and normalised; known-issues.md pruned to what is open | KIT-11, KIT-10 |
| A13 | Repo hygiene: `tools/wt_gc.py` (dry run first; skips trees with uncommitted work or a running lab/agent) for the 393 merged worktrees (100 GB of build/ and out/) and 428 merged branches | KIT-9 |
| A14 | Threads: one `game::on_main()`, a counter of off-main game calls, a Threads section in docs/01-architecture.md | CORE-6 |

### Wave B: foundations (S-M)

| Id | What | From |
|---|---|---|
| B1 | Fix-switch retirement: rule (released 3+ releases ago, a FAIL run on record via `lab.py fixcheck`, not on a keep list); delete the off branches of the first batch (the 429 from 0.7.9 and older that qualify), tests stay PASS-only; drop `coop.corefix` / `coop.sessionfix` aliases and the two "before BUG-0033" lists | CORE-1, STG-10 |
| B2 | coop.cpp split, pure moves: coop_cmds.cpp (~2300 lines), dna.cpp, vote.cpp, puppets.cpp, session_glue.cpp; `sporekit_mod_main` into named phases (also documents the frame order) | CORE-2, CORE-8 |
| B3 | One message manifest: `messages.def` / `space_kinds.def` X-macros generate msg_class, space_dir, the dispatch table and the docs table; unit test that every type is listed once; host-side direction gate for top-level types | NET-3, CORE-7 |
| B4 | Ratchet lints in `make test`: `object_alive` count may only go down; guard flags around `o_*` calls must be CALL_GUARDs; on_* session functions must be registered with lifecycle | STG-9, CORE-4, CORE-5, CORE-3 |
| B5 | Symbols: a coop/SporeKit debug-symbols asset per release; `crash_triage.py` symbolises `coop.dll+0x` frames with the report's version | KIT-3 |
| B6 | Compatibility: written N-1 policy, a `CAP_LEVEL` revision at join, a manifest hash in `make test` that forces the bump, the mixed-version test against the previous tag | NET-6 |

### Wave C: finish the shared mechanisms (M-L, one stage at a time)

| Id | What | From |
|---|---|---|
| C1 | One mirror registry (`mirror_registry.h`: bind / unbind, serial-based liveness via LiveRef, delete-unseen with a hold predicate, save filter); order NPC, Tribe, Civ (Cell stays as is); each stage's `object_alive` calls move to LiveRef with it | STG-1, CORE-4, BUG-0426 |
| C2 | One "world changed": a single world_id and lifecycle observers with explicit order and `needs_game`; the three detectors, the copied `world_check` and the hand reset lists go; Space joins lifecycle.h | STG-3, CORE-3, NET-1 (part), BUG-0427 |
| C3 | One host-busy model: the host sends its busy state (away, editor, pause) once; each stage declares a policy row; one shared pump for held commands (owner question 1: pause in every stage) | STG-4 |
| C4 | One snapshot receiver (`net::SnapshotReceiver`) for the chunked receive-and-delete of tribe, civ, squadron | STG-2 |
| C5 | Space kept state: one `KeptStore<T>` with a single handshake and restore window for map, trade, badges, rivalry (no wire change) | NET-1 |
| C6 | Space and pool payloads as portable structs (decode, check, apply) in fuzz_decoders, golden bytes; the libFuzzer build runs in CI | NET-4, NET-10 |
| C7 | Tribe sub-kinds get their own message type instead of MSG_NPC_ANIM's whitelist | STG-5 |
| C8 | tribe.cpp / civ.cpp / cellsync.cpp split along the seams C1-C4 leave (state grouped into structs first); the four `debug()` functions (4.5k lines) into their own files | STG-7 |

### Wave D: tests on the new structure

| Id | What | From |
|---|---|---|
| D1 | Thresholds: set between the measured PASS and FAIL ranges, scaled by fps where it matters; the scheduler budgets games, VRAM and Xwayland, not lanes | TST-3 |
| D2 | Warm-reset fingerprint: `coop.testreset` state fingerprint over the lifecycle registry; twoinst compares it with a cold start and falls back to cold on a mismatch | TST-4 |
| D3 | Rules into unit-testable headers: `tribe_logic.h`, `civ_logic.h`, NPC proxy, world-share queue, Civ money | STG-8, TST-9 |
| D4 | Shared test harness (`harness.py`: wait, poll, console parsing, wild-NPC pick) and a codemod over the tests; run_all.sh removed | TST-5 |
| D5 | Test impact selection (`lab.py run impact`) for in-between runs; the final check stays full | TST-7 |

### Not in 0.7.15

* A replicated per-member animation state for Tribe (STG-6): a spike first; 0.7.16.
* Unifying the three money ledgers (NET-11): 0.8 Rivals; 0.7.15 only clamps (A11).
* Update signing (KIT-2): needs the owner's key decision (question 5); can go in 0.7.15 if he decides early.
* Not worth doing at all (all reports): a full rewrite, an OOP / global-context redesign, protocol 8, DTLS, CMake,
  ASan of the DLLs under Wine, a blocking clang-tidy gate over coop (a non-blocking baseline job is fine, KIT-6),
  splitting files only for size, mass deletion of tests, a quick-mode final check, more lanes.

## Owner decisions

1. **Pause:** the host's pause makes guests wait only in Tribe today (docs/03-gameplay.md says "for everyone"). Should
   guests wait in every stage (C3), or should the doc change?
2. **Editor visit:** should the host's editor visit count as a world change for NPCs and nests (C2)?
3. **GOG 2016 code:** remove the frozen 2016 code and symbols in 0.7.15, or only fix the texts (A5)?
4. **Fix-switch retirement (B1):** CLAUDE.md 3 keeps the old behaviour "so the lab can check later". May switches
   older than 3 releases with a recorded FAIL run be deleted?
5. **Update signing (KIT-2):** do you want signed updates, and who keeps the private key (a GitHub secret used by CI,
   or a key only on your machine, which means you sign each release yourself)?
6. **Debug symbols (B5):** may they be a public release asset (they make the DLL easier to read for anyone)?
7. **Versions (B6):** refuse players with an older 0.7.x, or keep letting any 0.7.x in with a warning?
8. **Kick (A11):** a "kick" button in the co-op window, or a console command only?
9. **Cheating:** is a modified client cheating in the shared economy in scope (decides how strict the money checks are)?
10. **Final-check reruns:** may a rerun after the final check run only the failed campaign segment (A6) instead of the
    whole 82-minute campaign?
11. **Inconclusive:** may a test whose precondition did not happen report INCONCLUSIVE (not a pass, not a fail)?
12. **Second Xwayland:** may the lab start a second headless Xwayland to stop the shared one saturating (D1)?
