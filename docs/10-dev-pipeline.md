# 10. Development pipeline

How SporeCoop is developed and tested so that a release works for a normal player: many short
feedback loops on the developer's machine, every change tested on real game instances in
parallel, random players (bots) that do what nobody planned, and written-down knowledge.
Rules for every contributor and agent: [CLAUDE.md](../CLAUDE.md).

## 10.1 Where the time goes (measured 2026-10-22, Ryzen 5 7500F, 2 instances per lane)

| Step | Time |
|---|---|
| Wine + SporeKit up | 4 s |
| host: galaxy menu → planet loaded (`coop.load`) | ~10 s |
| guest: menu settles, joins | ~8 s |
| world to the guest: host save 2.5 s + transfer of 3.8 MB + load 0.6 s | ~8 s |
| **cold start of a pair** | **~40 s** |
| **warm reset** (same save: new slot, `coop.load` via the galaxy menu, guest rejoins) | **~7 s** (5–6 s before loads went through the menu) |
| stage switch in a running game (`coop.load`: save + quit to the galaxy menu, load) | host in the new stage 5–6 s, guest together 16–18 s |
| hot reload of coop.dll in running games | ~1 s |
| memory per game | ~480 MB (12 games ≈ 6 GB) |

The world transfer was capped at 32 packets per frame (~1.1 MB/s); a 128-packet ack window
makes it ~3 MB/s with 5 % loss (unit test `bulk transfer`).

## 10.2 Test lanes

A lane is an independent copy of the whole setup, so several test pairs run at the same time:

* lane 1 = the game folder that contains this repository (the repository is cloned into the
  game folder, next to `SporebinEP1`), in the Bottles bottle `SPORE_BOTTLE` (default `Games`);
* lane N ≥ 2 = `tools/spore.sh lane create N`: reflink copies (Btrfs: no extra space) of the game
  folder (`SPORE-lab/laneN`: own `mods/`, logs, `SporeCoop.package`) and of the bottle
  (`SporeLabN`: own wineserver, so a crash or quit stays in the lane; registry paths point to the
  copy); takes 2 s. A game started while the lane's wineserver runs joins that wineserver's PID namespace
  (`flatpak run --parent-pid --parent-share-pids`, BUG-1327, docs/06-testing.md#testing-lane-pid-namespace).
* instances `2N-1` (host) / `2N` (guest): console ports 47800 + i − 1, co-op port 47810 + 2(N−1);
  test profiles `test` / `inst<2N>`; outputs under `out/laneN/`.
* `SPORE_LANE=N` selects a lane for `tools/spore.sh`, every test and helper (`tools/tests/lane.py`).
* Lanes 1..10 (ports, `tools/tests/lane.py`); at most 6 at once on this machine (`lab.py` caps it:
  a GPU reset under more load, docs/06-testing.md 6.19). Since 2026-10-24 every lane is a copy of
  the GOG 2024 install (`dc04aee5`, the only supported build). GOG 2016 is not supported and has no
  symbol files since 0.7.15 (BUG-1125): a 2016 exe in a lane gets the "not supported" message, no co-op.

## 10.3 The lab (`tools/lab.py`)

```
tools/lab.py run smoke --lanes 3                 # before a merge (~8 min)
tools/lab.py run full --lanes 3 --branch feat/x  # a branch: its own worktree + build
tools/lab.py run tribe_play,select               # chosen tests
tools/lab.py run chaos_tribe --repeat 10 --retry 0   # hunt a flaky bug
tools/lab.py reload --lane 2                     # dev loop: build + hot reload in running games
```

* Tests and their properties: `tools/tests/suite.py` (suites smoke / full / nightly, warm,
  exclusive, length, save).
* Scheduling: one test per lane at a time, longest first; a lane prefers a test on the save it
  has loaded (warm reset instead of a restart); exclusive tests (LAN discovery) run alone at the end.
  They run on the run's first lane among 1-5 (LAN discovery sees only those) and are skipped when
  the run has none; until 2026-10-23 they always took lane 1, even with `--skip 1` (a run on lanes
  7/8 started the UI test in the real install's bottle while other agents had lane 1).
* After a failure the lane restarts cold. Only harness trouble (no RESULT line, a traceback, a
  timeout) runs once more: passing then = **flaky** (`ok (flaky)`, the run still passes, the
  summary counts it). A `RESULT: FAIL`, a crash or a fault is a failure of the run, never retried
  (until 2026-10-28 every failure was retried and a pass on the retry passed the run: BUG-0073).
  `out/lab/flaky.json` keeps the history; the summary lists the unstable tests - each one is a
  bug in the test or in the mod and gets fixed, not ignored.
* `RESULT: SKIP` is shown as `SKIP` / `skip` and counted apart ("PASSED: 14/15, 1 skipped"), never
  as a pass (BUG-0100). Since 0.7.15 (A15) it is the verdict INCONCLUSIVE, and INCONCLUSIVE three
  runs in a row is a failure: [Lab verdicts](#pipeline-lab-verdicts).
* A timeout (30 min) kills the test's whole process group: tests run in their own session
  (BUG-0052: the shell died, the Python test went on driving the lane). Ctrl+C on the lab kills
  the running tests too.
* `--branch <b>` takes only the worktree whose branch is exactly `<b>` (BUG-0048: `fix/cell` took
  the tree of `fix/cell-meat`) and stops when the tree has another branch checked out; the run
  header and `summary.md` name the tree's branch.
* Self-test of the lab: `tools/tests/lab_selftest.py` (in `make test`), fake tests through
  `lab.cmd_run` with the lanes stubbed; `--tree <checkout>` runs it against another checkout's lab.
* Results: `out/lab/<run>/summary.md`, `results.json`, the test logs and both games' own logs.
* The machine budget (0.7.15, BUG-1114, docs/06-testing.md#testing-lab-budget): after the memory gate a test waits
  until the whole machine has room for the games it adds (at most 18 games, the card's VRAM, at most 10 games and
  under 80 % Xwayland on its display, 12 games while a motion test runs); lane N draws on headless shell
  (N - 1) % 2 (docs/06-testing.md#testing-two-displays); the start-up steps wait on signals, not fixed sleeps
  (docs/06-testing.md#testing-start-waits).

## 10.4 Invariants (checked in every test)

`twoinst.start()` starts a monitor for both games; `Checks.faults()` fails the test when:

* a game's frames stop for more than 10 s (frozen; the SporeKit watchdog writes the main thread
  stack into the game log after 30 s);
* a game stops answering (crash);
* the exception logger caught a fault (`exc`);
* a fixture lacks what the test needs (`tools/tests/fixtures.py` facts; since 2026-10-28 for real,
  `twoinst.FIXTURE_BAD`; a warm reset that lacks them falls back to a cold start, BUG-0124).

The monitor finds the game log (`loading()`: a world load gets 60 s instead of 10 s) with
`lane.game_log()` / `lane.game_dir()`, right from a worktree too (BUG-0068: from
`../SporeCoop-wt/<branch>` it read a folder that does not exist and long loads became FROZE).

`tools/lab.py` checks every test from outside as well (BUG-0036, BUG-0042), so a test that forgets
`Checks.faults()` or crashes after its checks still fails:

* a new `crash-*.txt`, `crash-*.dmp` or `report-*.txt` in the lane's `mods/logs` between the start
  of the test and 5 s after it (copied into the run directory as `<test>.<file>`);
* a game of the lane that answered its console when the test ended and no longer answers 5 s later;
* a line `<name> faults: total faults: N` with N > 0 in the test's log.

Tests that make reports or faults on purpose (diag, kit_crash, kit_freeze, exit_thread) have
`reports=True` in `suite.py`.

## 10.5 Bots

* **Scenario tests** (`coop_*.py`) - one feature each, with checks.
* **Chaos bot** (`coop_chaos.py`) - both players act at random but validly: clicks on own and
  partner units (`coop.targets`), selection boxes (`drag`), orders, keys, the co-op window, the
  pause menu, stage actions, rejoins, saves, network outages. Seeded: the printed replay line
  repeats the same actions. Smoke: 2 min of tribe; nightly: every stage, minutes each.
* **Bad network** (`netem.py`): `SPORE_NET=dsl|mobile|awful|delay=..,jitter=..,loss=..` puts a UDP
  proxy with delay, jitter, loss, duplicates and outages between guest and host - for any test.
  The proxy waits on both sockets with `select()` and drains them; until 2026-10-23 it polled each
  socket with a 5 ms timeout and so capped the link at ~200 packets/s (~200 KB/s, plus kernel
  drops of its full receive buffer): every world transfer over it took ~21-24 s whatever the preset.

## 10.6 Fixtures (test saves)

`out/fixtures/<name>.tgz` (main checkout, shared by worktrees), described in
`tools/tests/fixtures.py` (planet, stage, facts). New ones: `tools/tests/make_fixture.py <base>
<name> --cmd "…"` (load, run commands, save, pack) or `new_stage_game.py` for a new stage game.
Warm resets put a fixture into a fresh slot (`Game10`…`Game19`): the files of the loaded save
must not change under the game.

## 10.7 Levels

| When | What | Where |
|---|---|---|
| every build | `make test`: unit tests (netcode, DBPF, discovery), `re_lint` (symbols known, confidence) | local + GitHub Actions |
| after a change | the feature's test on a warm lane, `lab.py reload` for the next try | local |
| before a merge to `main` | `lab.py run smoke --branch <b>` passes | local |
| nightly 03:30 | `tools/nightly.sh`: the nightly suite on `main`, desktop notification | local (systemd user timer) |
| before a release | nightly green + manual play-test checklist (two people, every stage) | local |

GitHub Actions (`.github/workflows/build.yml`) only builds and runs the unit tests: game tests
need Spore, and a self-hosted runner would let anyone's pull request run code on this machine.

Nightly timer (from the repository root):

```bash
sed "s|@REPO@|$PWD|g" tools/systemd/sporecoop-nightly.service > ~/.config/systemd/user/sporecoop-nightly.service
cp tools/systemd/sporecoop-nightly.timer ~/.config/systemd/user/
systemctl --user daemon-reload && systemctl --user enable --now sporecoop-nightly.timer
```

## 10.8 Git

`main` always works. One topic per branch (`feat/…`, `fix/…`), each in its worktree
(`../SporeCoop-wt/<branch>`, made by `lab.py run --branch`); small commits; a pull request with the
lab summary; merge after smoke passes. A bug found anywhere → `docs/known-issues.md` or a GitHub
issue → a test that reproduces it → the fix.

## 10.9 Knowledge

Search before analysing, write down after learning: `data/symbols/overrides_*.sym` (functions with
confidence and signature), `docs/re/<area>.md`, `docs/known-issues.md`, this file and
`docs/06-testing.md`. `tools/re_lint.py` keeps the symbol file honest. Subagents get the same rule
in their prompt.

## Batches of agents: the brief (2026-10-25)
<a id="pipeline-agent-brief"></a>

Player reports and work packages are handed to parallel agents, one per branch, worktree and lane.
The steps an agent follows are in the skill `.claude/skills/fix-player-report` (the orchestrator's
merge steps: `.claude/skills/merge-batch`), so a brief only says what is specific:

```
Fix player report BUG-NNNN: <one line> (source: issue #N | report zip path | screenshot text below)
Branch fix/<name>, worktree ../SporeCoop-wt/fix/<name>, lane <N> only.
Follow .claude/skills/fix-player-report and return its report format.
Context: <known-issues anchor, docs/re anchor, tools/crash_triage.py output (skill triage-crash)>
Others in this batch: <branches and the files or areas they own>
```

Agents append one line per finding to `out/findings.md` in the main checkout (not in git:
`<date> <branch> <area>: <finding> (<anchor>)`), and read it before analysing, so siblings in the
same batch do not find the same thing twice; the orchestrator folds the lines into the docs when it
merges. Practices taken over from a review of another co-op modding project (2026-10-25): unnumbered
sections with anchors, CORRECTION lines, union merges of shared files, "a test you never saw fail
proves nothing", oracles on the other machine, crash triage by signature (CLAUDE.md).

## Dev tools never touch the owner's game (2026-10-28, fix/w4-tools)
<a id="pipeline-owner-safety"></a>

* Lane 1 is the real install the owner plays from. `spore.sh kill` there ends only games started
  for tests (a `-sporekit-profile:` or `-sporekit-instance:` argument); the owner's own Spore is
  never killed and, while it runs, no Wine tree of the "Games" bottle either (its wineserver may sit
  in a test's launch sandbox). On lanes 2..10 it ends every game of the lane and whatever runs in
  the lane's bottle, never another lane's (a half-dead game is matched by a live thread's command
  line). `spore.sh pids [inst]` lists what it would end (BUG-0025, BUG-0101, BUG-0103).
* `lab.py kill` cleans lanes 2..10; `--lane N` one lane, `--upto N` lanes 2..N, `--lanes 2,3` a list (a
  bare `--lanes 3` is refused as ambiguous, BUG-0253); lane 1 only with `--include-lane-1` (test games
  only). It prints the lanes it ends and exits 1 when none is left.
* `spore.sh fixture restore|slot` refuses the player's own profile `-` (only `save` reads from it)
  and a missing fixture before it deletes anything (BUG-0102).
* A test profile redirects Documents ("My Spore Creations") as well as AppData (BUG-0156):
  `AppData\Roaming\SporeKit-<profile>\Documents`. `-sporekit-profile-docs:0` turns that off (the
  old behaviour, for `kit_profile_docs.py --old`).
* `headless.sh down` kills the process group of its pgid file only while the group's leader still
  runs with the headless runtime folder (BUG-0207).
* `tools/tests/tools_selftest.py` (in `make test`) checks all of this on fakes: a private PID
  namespace (`unshare -Urpf --mount-proc`) with fake game processes, a fake HOME and bottle, a
  temporary git repository. `SELFTEST_<TOOL>=<old copy>` runs a check against an older tool.

## Releases: the tag, COOP_VERSION and the notes (2026-10-28, fix/w4-tools)
<a id="pipeline-release-check"></a>

`tools/release_check.sh vX.Y.Z [notes.md]` (run by the build and release jobs on a tag, and
locally before tagging) fails when the tag is not `COOP_VERSION` (the updater would offer an update
that never applies) or when CHANGELOG.md has no `## [X.Y.Z]` section, and writes exactly that
section as the release notes (not the X.Y.Z.N sections after it) (BUG-0097). Before a release
also check README (Compatibility, footnotes) and INSTALL, which name no patch versions any more
(BUG-0165).

## Worktrees and branches that are done: wt_gc.py (2026-11-15, feat/w51-hygiene)
<a id="pipeline-wt-gc"></a>

Every agent and `lab.py run --branch` makes a worktree in `../SporeCoop-wt/`; nothing removed them (KIT-9 of
docs/audit-0.7.15/kit_build.md: 400 trees, about 100 GB in their build/ and out/). `tools/wt_gc.py` (BUG-1127)
prints what can go and how many bytes of build/ and out/ that frees; only `--apply` deletes (`git worktree remove`
without `--force`, `git branch -d`, `git worktree prune`). A tree goes when its branch is merged into main (not main,
not `rel/*`, not a `--keep` glob, no detached HEAD), `git status` is empty and no merge / rebase / bisect is in
progress, nothing uses it (a process's cwd, PWD, open file or command-line path in it; a held lab lane lock
`.lab-run-laneN.lock` or a game console port 47800+ of a process started from it) and nothing in it changed for
`--min-age` hours (48: its git HEAD / index, top folder, out/, out/lab/*, build/). Merged branches without a tree go
with `git branch -d`; unmerged ones are listed for a person. Run the dry run first and read its "kept" lines; during a
release batch keep its trees: `tools/wt_gc.py --keep '*/w51-*'`. Self-test: `tools_selftest.py wtgc` (a temporary
repository with a busy tree, a held lane lock, uncommitted work, an unmerged branch).

Dry run on 2026-11-15 (main 74611a57, `--keep '*/w51-*'`, nothing deleted): 307 worktrees to remove with 77.5 GB of
build/ and out/, 342 merged branches; kept 108 trees (68 changed in the last 48 h, 16 `rel/*`, 8 unmerged, 6
detached, 6 `*/w51-*`, 3 in use by a process, 1 with uncommitted work); 16 unmerged branches left for a person.

## CI supply chain: pinned actions and toolchain (2026-11-15, feat/w51-build)
<a id="pipeline-ci-pins"></a>

* `tools/toolchain.env` is the one place for the build environment: the llvm-mingw version and the
  SHA-256 of its tarball, the CI runner's Python (`CI_PYTHON`) and Pillow. The Makefile includes it
  (`make toolchain-path` prints the folder it uses), `tools/package.sh` asks the Makefile,
  `tools/release_dry.sh` reads the file of the commit it checks, and the workflow copies its
  `KEY=value` lines into `$GITHUB_ENV`.
* The workflow checks the tarball with `sha256sum -c` before unpacking it (a changed or replaced
  asset fails the job), installs `pillow==$PILLOW_VERSION`, and fails when the runner's `python3`
  is not `CI_PYTHON` (then `make release-dry` would test another Python than CI runs).
* Every action is pinned by its commit SHA, the tag it was resolved from in a comment
  (`actions/checkout@<sha> # v4.4.0`). New version: `gh api repos/<owner>/<action>/commits/<tag>
  --jq .sha`. The pinned tags are the latest of the majors that ran green (v4 / v2); GitHub runs
  their Node 20 code on Node 24 with a warning. A major bump is its own change with a CI run.
* New toolchain: download the tarball, `sha256sum` it, compare with the asset's `digest` in
  `gh api repos/mstorsjo/llvm-mingw/releases/tags/<version>`, update both lines.
* `tools/ci_lint.py` (in `make test`) fails on an action without a SHA or tag comment, a workflow
  that does not check the tarball or pins a toolchain version of its own, unpinned Pillow, or a
  malformed `toolchain.env` (BUG-1121).

## Release dry run, and tag only on a green main (2026-11-15, feat/w51-build)
<a id="pipeline-release-dry"></a>

3 of 9 tags before 0.7.15 failed only in CI (v0.7.3.4: libstdc++ 12 headers; v0.7.11: Python 3.12
f-strings that the runner's 3.10 cannot parse; v0.7.14: a private ticket id in the notes, invisible
without out/tickets), and v0.7.14 was moved after its failure. `make release-dry`
(`tools/release_dry.sh [--rev <commit>] [--keep]`, about 95 s) runs the tag workflow locally (BUG-1120):

* a `git archive` copy of the commit (HEAD by default; uncommitted changes are not in it) in an empty
  folder, made a depth-1 shallow repository of the same commit, as CI's checkout: no history, no
  out/ (no private tickets, lab results or old dist), no untracked files;
* CI's Python (`CI_PYTHON` of tools/toolchain.env, through `uv run --no-project --python 3.10`) as
  `python3`, Pillow `PILLOW_VERSION`, the pinned llvm-mingw, `env -i` with `CI=true` and `LANG=C.UTF-8`;
* the workflow's steps in order: the Python check, `release_check.sh v<COOP_VERSION>` (version, notes,
  notes-check), `make`, `make test`, `package.sh`, then the release job's checks (every asset the
  publish step names, SHA256SUMS.txt, the zip holds the attested binaries).
* Not covered: CI's host compiler for `make test` (ubuntu-22.04's clang 14 with libstdc++ 12, here
  Fedora's clang++: the v0.7.3.4 class of missing includes still shows only in CI), the upload and the
  attestation.
* Proof (2026-11-15): HEAD PASS; a commit with a 3.12 f-string in a game test (3.14 parses it) FAIL in
  `make test` (test_lint: does not parse); a commit naming a private ticket under Fixed FAIL in
  release_check (no ticket docs/tickets/BUG-1003-*.md).
* The copy is removed after a PASS and kept with `release-dry.log` after a FAIL; uv's relocatable
  Python gets `PYTHONHOME` there (a test starts it under a fake argv[0]).

**The release order** (replaces "push main and the tag together"):

1. On main: CHANGELOG section, `COOP_VERSION`, `tools/tickets.py release X.Y.Z`, commit.
2. `make release-dry` on that commit: PASS.
3. Push main only, then wait for its CI run: `gh run watch $(gh run list -b main -L 1 --json databaseId
   --jq '.[0].databaseId') --exit-status`.
4. Only on a green main: push the tag `vX.Y.Z` on exactly that commit.
5. A published tag is never moved or re-pushed: a broken release gets the next version (X.Y.Z.1).

## Debug symbols per release (2026-11-15, feat/w51-build)
<a id="pipeline-release-symbols"></a>

Owner decision 6 (0.7.15 plan): the debug symbols are a public release asset. `tools/package.sh` calls
`tools/symbols_pack.sh`, which writes `out/dist/SporeCoop-<ver>-debug-symbols.tar.xz` (the unstripped
DLLs of the build, `SYMBOLS.txt`); the build job uploads it with the other files, the release job
fails when it is missing, lists it in SHA256SUMS.txt, publishes and attests it (BUG-1123). The player
files stay stripped. `make release-dry` checks that the asset exists. How triage uses it:
docs/06-testing.md#testing-crash-symbols.

## Compiler flags of the DLLs: no aliasing assumptions, warnings are errors (2026-11-15, feat/w51-build)
<a id="pipeline-compiler-flags"></a>

* `COMMON` in the Makefile: `-fno-strict-aliasing -fno-delete-null-pointer-checks` (game memory is read and
  written through casts at fixed offsets, `field<T>` / `F<T>`, and SporeKit lets code go on after a repaired
  fault: the compiler may not reorder such accesses by type or drop a null check after a dereference),
  `-Wimplicit-fallthrough` and `-Werror` (BUG-1122). The cost: .text +1.35 % (coop.dll), +2.4 % (sporekit.dll).
* Printf-style functions carry `SK_PRINTF(format arg, first vararg)` (sdk/include/sporekit.h; 0 for a
  `va_list`): `H->log`, `H->logv`, `H->out_printf`, `sk::logf` / `logv` / `out_vappend` and the wrappers; a new
  one gets it too. A `%s` with an int or `%d` with a 64-bit value is then a build error, not a 32-bit vararg
  crash at runtime.
* A new warning fails the build: fix it (a counter nobody reads goes, an offset kept as knowledge gets
  `[[maybe_unused]]`). The host test builds (`make test`, HOSTCXX) keep `-Wall` without `-Werror`: CI's host
  clang 14 and the local clang differ.
* `-Wformat=2` is not on: its 15 `-Wformat-nonliteral` hits are the translated format strings of the UI.

## Lab verdicts, the flaky ledger, repeats and impact runs (2026-11-15, feat/w51-labverdict)
<a id="pipeline-lab-verdicts"></a>

0.7.15 A6, A7, A8, A15, D5 (BUG-1115..1119); the details and the rules are in
docs/06-testing.md#testing-lab-verdicts, #testing-campaign-segments and #testing-impact.

```
tools/lab.py flaky [--branch 'rel/*'] [--days 14] [--test X]   # one flake history over every worktree
tools/lab.py run X --repeat 20 [--until clear|fail|all]        # stops once the answer is clear, prints the rate
tools/lab.py impact --why [--base rel/0.7.15]                  # which tests a change can break, and why
tools/lab.py run impact --lane 2 --lane 3 --retry 0 --no-build # run them (in-between runs only)
tools/lab.py run campaign_segment --segment <lab run>          # a failed campaign's segment again, from its save
```

* Verdicts: PASS, FAIL, INCONCLUSIVE. A test whose precondition did not happen prints `RESULT: INCONCLUSIVE <why>`
  (`RESULT: SKIP <why>` means the same). It is never a pass: shown `INC` / `INCONCLUSIVE`, counted apart ("PASSED:
  14/15, 1 INCONCLUSIVE (not passed)"); one does not fail the run; the third in a row (the ledger, every worktree,
  twins and quick runs left out) fails the test with `FAIL lab: INCONCLUSIVE 3 runs in a row ...: ticket candidate`.
  INCONCLUSIVE is neither half of a FAIL/PASS twin (`lab.py fixcheck`).
* Every failure has a reason in results.json / summary.md: the FAIL lines, else a campaign's findings, a traceback's
  exception, the timeout, "no RESULT line".
* results.json records per result `verdict`, `fix_off` (SPORE_FIX_OFF / SPORE_LAB_FIXOFF), `twin` (planted old
  behaviour in the lab's environment), `segment` (a failed campaign); summary.md lists each failed or INCONCLUSIVE
  test's history ("known flake: 5 of 11 failed on rel/main ...") and the ticket candidates. `out/lab/flaky.json` is
  still written (per checkout) but no longer read by the summary.
* The final check and the release rule stay full (`run impact` is for fix branches and the integration runs between
  merges); the full campaign runs once in the final check, a rerun may run only its failed segment (owner decision 10).

## Proving a refactoring: pure moves and identical dumps (2026-11-15, feat/w51-split)
<a id="pipeline-refactor-proof"></a>

The 0.7.15 rule for refactoring (docs/plan-0.7.15.md "Rules for this release"): a pure move (a file split, a
function moved) carries no edits in its commit, and it is proved by the build, `make test` and identical dumps of
the registries before and after. `tools/refactor_dump.py` makes and compares them (B2, BUG-1128; B1 and C8 reuse it).

1. Build the base (`make -j8`) and take its dumps on your lane:
   `SPORE_DETECTOR=20 python3 tools/lab.py run refactor_dump --lane N --retry 0 --no-build`
   (suite `refactor`, only by name; about 1 min: a cold start of two games in a Creature session). The log
   `out/lab/<run>/refactor_dump.log` holds `=== <section>` blocks of both games: `coop.fix list` (count line, then
   the switches sorted: their registration order is the link order, which a new file changes), `coop.lifecycle`
   (in table order, the reset order; held counts dropped), `coop.players`, `coop.guard` (names sorted), SporeKit's
   `hooks` (in the order they were made) and `help` (every command with its help text), and two tables from the
   source: the message dispatch of `g_sess.cb.custom` (every `MSG_` test and `on_message` call in order, and the
   count of each `net::MSG_` name over mods/coop) and the session callbacks with a hash of each lambda's body.
2. Make the move, build, run `make test`, take the dumps again.
3. `python3 tools/refactor_dump.py compare out/lab/<base run> out/lab/<new run>`: every section must say identical
   (exit 1 otherwise, with a diff).
4. `python3 tools/refactor_dump.py moves <base rev> <new rev>` (`.` = the working tree) lists every line of
   mods/coop/*.cpp|h whose count changed, i.e. what the commit did besides moving lines. For a pure move it shows
   only includes, declarations, `static` dropped where a name became shared, a default argument kept only on the
   prototype, wrapper heads and braces. Name these in the commit message.
5. Behaviour-level check once at the end of a series: the smoke tests on the lane.

Helpers for a split: a definition another file uses loses `static` and gets a declaration in the shared header (for
the coop core: mods/coop/coop_state.h, docs/01-architecture.md#arch-start-and-frame); a type moves into the header
with its comment; a `static` forward declaration of a moved function loses `static` too (clang: "static declaration
follows non-static declaration", or the warning "has internal linkage but is not defined", which becomes a link
error). B2's commits are the worked example (git log --grep BUG-1128).

## Retiring fix switches (2026-11-15, feat/w51-retire)
<a id="pipeline-fix-retire"></a>

Every bug fix keeps its old code behind a `coop.fix` switch (CLAUDE.md 3, docs/06-testing.md#testing-fix-registry),
so the lab can show that its test catches the bug. Kept forever, the off paths pile up: 779 switches in 0.7.14, none
ever removed, and an old switch's off side becomes a mix of code nobody shipped (docs/audit-0.7.15/core.md CORE-1).
Owner decision 4 of docs/plan-0.7.15.md: retire them by a rule, in batches (B1, BUG-1138).

**The rule.** A switch may retire when all three hold:
1. its ticket is fixed and was released 3 or more releases before the current one (hotfixes `X.Y.Z.W` count with
   `X.Y.Z`; for 0.7.15 that is 0.7.12 and older);
2. a FAIL run with the switch off is on record: the ticket names a FAIL run id (`YYYYMMDD-HHMMSS` in a sentence that
   says FAIL), or a recorded lab result whose test output says `SPORE_FIX_OFF: <switch> off` failed (what
   `lab.py fixcheck` reads; an INCONCLUSIVE result is no FAIL);
3. it is on no keep list.

**The keep list** (a switch stays even when 1 and 2 hold):
* `untested`: no test or lab tool names it (152 in the audit, owner decision): no FAIL run can be made in the lab;
* `space`: it is registered in or read by a Space file (space*.cpp, squadron.cpp, spacecoop.cpp) or shared.cpp, or
  its area is `space` / `fleet` / `squadron` (the Space package of 0.7.15 rewrites these files);
* `unit`: a portable switch (fix_table.cpp: mods/coop/net/, updater/) or one whose off side the unit tests run
  (tests/*.cpp, a header function that takes the switch as an argument);
* `seam`: another test switches it off for its own setup (the monitor's planted faults in coop_monitor_plant, a
  test that checks the off side in its PASS run, a suite entry run with it off), or its off path is a documented
  user or debug option; listed with the reason in `KEEP` of tools/fix_retire.py;
* `manual`: removing the off path is not a plain deletion (the bool is also state, the off and on paths are
  interleaved, a table or a hook depends on it); found while retiring, added to `KEEP` with the reason.

**The tool.** `python3 tools/lab.py fixcheck --retire [--upto 0.7.9] [--all]` (= `tools/fix_retire.py list`)
prints the candidates with their evidence (the ticket's run ids, the recorded FAIL runs); `--all` also prints the
kept ones and why, `--json` the whole inventory. `tools/fix_retired.txt` lists every retired switch (name, ticket,
fixed in, retired in, the FAIL run on record) and the old toggle commands removed with them.
`tools/fix_retire.py check` (in `make test`, with `--self-test`) fails when a retired name is registered again (a
new fix takes a new name), when a test or a lab tool names it (a line that says the switch is retired, with its FAIL
run, may), when a current doc switches it off (`coop.fix <name>`, `SPORE_FIX_OFF=`; tickets, the CHANGELOG,
docs/archive, the audits and plans keep their history, and docs/re keeps the name as the fix's label), or when a
removed command is used anywhere. `tools/tickets.py check` knows the retired names (a ticket's `switch:` stays).

**Retiring one switch.** Delete the `COOP_FIX` line and the bool; at every use the fixed path stays exactly as it
ran with the switch on (`if (!x) return;` goes, `x && c` becomes `c`, `x ? a : b` becomes `a`, code only the off
path used goes too, a status line drops its `fix=` field unless a test parses it); the old per-module toggles of
the switch (`coop.pool fix <alias>`, `coop.civ fix <alias>`, ...) go; comments describe what the code does now with
the ticket id. Its tests stay as PASS-only regression tests: their `--nofix` / `SPORE_FIX_OFF` twin goes, and the
claim becomes "<switch> off failed (retired in 0.7.15, BUG-1138: FAIL run <run>)". The ticket's History gets
"switch retired in 0.7.15, FAIL run <dir> on record". Proof per batch: `make -j8`, `make test`, the refactor dumps
before and after (docs/10-dev-pipeline.md#pipeline-refactor-proof: the only differences are the retired switches in
the `fix` section and the removed toggle commands in `commands`), and `lab.py run impact --base <base>`.

## Finding what a file split shares, from the compiler (2026-11-15, feat/w51-cellsplit)
<a id="pipeline-split-ast"></a>

A split of a 7k-line file by hand misses names. The Cell split (BUG-1183, docs/01-architecture.md#arch-cell-files)
took the list from clang instead: `i686-w64-mingw32-clang++ <the build's -std / -D / -I flags> -fsyntax-only -Xclang
-ast-dump=json -Xclang -ast-dump-filter=<namespace> mods/coop/<file>.cpp` (about 100 MB of JSON, 3 s) gives every
top-level declaration of the namespace with its byte range, its `previousDecl` (forward declarations) and, inside it,
every `referencedDecl` id. Assign the declarations to the new files by line ranges; a variable or function used from
another file than its definition's loses `static` (on every declaration) and gets an `extern` / a prototype in the
`_internal.h`; a type, constant, using-declaration or template another file names (also in such a declaration's type)
moves into the header, templates after the declarations they call. Pitfalls met: the JSON omits `file` and `line`
when they did not change since the previous location (track them in document order, or count lines from `offset`); a
macro's location has `spellingLoc` / `expansionLoc` (take the expansion); a prototype taken from a definition's head
must drop comments between the head and the body; a default argument stays on the header's prototype only (cut from
the `=`, not from the end of the parameter name: `bool patch[3] = nullptr`); keep every LIFECYCLE / WORLD_STATE /
SESSION_STATE line in the original file (`keep` ranges), and name the new files `<file>_*.cpp` so they link after it
(the static-init order of the original's registrations stays).
