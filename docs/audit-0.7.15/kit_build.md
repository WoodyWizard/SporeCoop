# SporeCoop 0.7.15 audit: SporeKit, build, CI, release, docs, repository hygiene

Auditor area: sporekit/, Makefile and toolchain, static analysis, sanitizers, CI, packaging, installer, updater,
user-facing docs, crash report pipeline, repository hygiene. Base: main at a762f240 (v0.7.14). Read-only: nothing in
the repository was changed; builds ran in a `git archive` copy under the scratchpad.

## Summary verdict

* SporeKit is the healthiest part of the code base: 10 082 lines, 1 compiler warning under `-Wall`, 15 under
  `-Wall -Wextra -Wshadow -Wconversion -Wformat=2`, and nearly every clang-analyzer hit is deliberate raw stack or
  crash-test code. Defensive, heavily commented, every fix traced to a ticket. Its bugs come from concurrency, Wine
  thread semantics, fault-handler re-entry and hot reload, which static tools do not find.
* The build is fast (clean build 26 s at -j8, `make test` 44 s) and the sanitizer setup is thoughtful, but it covers
  only the host-portable code (net, decoders, validators, the setup's release state).
* The weakest link is the **release path**: 3 of the last 9 tag builds failed only in CI (environment drift: Python
  3.10 vs 3.14, libstdc++ 12 vs Fedora headers, `out/tickets` present only locally), and the v0.7.14 tag was moved.
* Supply chain: the in-game updater installs DLLs checked only against a SHA256SUMS.txt from the same GitHub
  release (no signature); CI actions are pinned by tag, the toolchain tarball has no checksum.
* Player crash reports with `coop.dll+0x...` frames cannot be symbolized reliably: release DLLs are stripped and no
  debug symbols are kept per release.
* Hygiene debt is large but cheap to pay: 405 worktrees (393 on merged branches, 0 dirty) holding 108 GB, 436
  branches (428 merged), docs that grew append-only (06-testing.md 7 898 lines, known-issues.md 3 619 lines with
  only 7 open tickets in it), and stale GOG 2016 texts that contradict each other, including in the setup's UI.

## Metrics

| Metric | Value | How measured |
|---|---|---|
| sporekit/ size | 10 082 lines, 31 .cpp/.h; biggest diag.cpp 1 195, commands.cpp 1 026, d3d9hook.cpp 861 | `wc -l sporekit/*` |
| coop + net | 89 827 lines in 63 + 7 .cpp files | `cat mods/coop/*.cpp mods/coop/net/*.cpp \| wc -l` |
| Clean build (all targets) | 26.4 s wall at -j8 (2m52 s CPU) | `/usr/bin/time make -j8 all` in a `git archive` copy |
| `make test` | 43.7 s wall; fails in a non-git copy (docs_lint calls `git ls-files`) | same copy |
| Warnings at the shipped flags (`-Wall`) | 21: 17 unused-but-set-global, 2 unused-const-variable, 1 dangling-else (mods/coop/game.cpp:1818), 1 deprecated-volatile (sporekit/exceptions.cpp:433); 20 of 21 in mods/coop | build log |
| Extra warnings, sporekit (`-Wextra -Wshadow -Wconversion -Wformat=2 -Wimplicit-fallthrough`) | 15: 7 implicit-int-conversion, 4 int-float, 3 missing-format-attribute, 1 missing-field-initializers | `-fsyntax-only` per file |
| Extra warnings, coop/updater/installer (`-Wextra -Wshadow -Wimplicit-fallthrough`) | 67 unique: 20 shadow, 27 field initializers, 17 unused-but-set-global, 2 unused const, 1 dangling-else | same |
| printf format mismatches if SkHost::log / out_printf had `format(printf)` attributes | **0** over ~500 `H->log(` and ~208 `out_printf(` calls in coop and sporekit | header copy with the attributes, whole tree compiled with `-Wformat` |
| clang-tidy (in llvm-mingw, no install needed) on sporekit+updater+installer+autoupdate, bugprone/analyzer/performance | 189 signed-bitwise, 84 cert-err33-c (unchecked return), 57 no-int-to-ptr, 55 enum-size, 38 unchecked string-to-number, 7 analyzer hits (all deliberate: stack scans past a local marker, crash tests), 3 empty-catch, 3 suspicious-string-compare (implicit `!wcscmp`) | 23 s wall |
| clang-analyzer + curated bugprone on all of coop | 103 s wall, 46 unique: 11 incorrect-roundings, 9 branch-clone, 10 null-pointer-arithmetic (`field()` helpers), 6 excessive padding, 3 null deref, 2 dead stores, 1 garbage value; none confirmed real in a spot check (npc.cpp:3343, nest.cpp:840 read as false positives) | xargs -P10 |
| Sanitizer coverage | ASan+UBSan: net_tests (8 shards, plus `--nofix`, `--old-dbbf`, `--old-refpack` must die), fuzz_decoders, validate/cell/npc/space/text/jpeg/fixes host tests. TSan: release_tsan (setup state) only. Nothing of sporekit except jpeg.cpp runs on the host | Makefile:105-200 |
| CI | 1 workflow, build + `make test` + package on every push, release on tag; last 60 runs: 53 success, 7 failure; tag failures v0.7.3.4, v0.7.11, v0.7.14 (first push) | `gh run list` |
| Release binaries | stripped; unstripped coop.dll 74 MB (12 MB as xz); no debug symbols kept per release | `ls -la build`, `xz -9` |
| Fix switches | 779 COOP_FIX (152 not named by any test), 37 KitFix | grep |
| Tickets | 1 102 + README; kind: bug 511, desync 209, test 155, crash 118, tooling 57, feature 36, freeze 16; 38 distinct `stage` spellings | frontmatter parse |
| Tickets in this area (text names sporekit/updater/installer/loader/Makefile/CI files) | sporekit 60 (bug 27, crash 12, freeze 7, tooling 7), updater/installer 27, crash report pipeline 93 (crash 41) | regex over ticket bodies |
| Git churn (all history, 3 653 commits since 2026-10-01) | sporekit 93 commits (43 with BUG ids; core.h 53, commands.cpp 32, fixes.cpp 23, diag.cpp 23), Makefile 50, installer 13, updater 12, .github 8 | `git log -- <path>` |
| Worktrees / branches | 406 worktrees (405 + main), 393 on branches merged into main, 5 detached, 0 with tracked changes; 108 GB in ../SporeCoop-wt, 100 GB of it in their build/ and out/; 436 branches, 428 merged | `git worktree list`, `git branch --merged` |
| Repository | .git 231 MB (125 MB loose objects, 33 MB packed), 2 212 tracked files; docs/ 18 MB (media 8 MB, tickets 6.5 MB, re 1.6 MB); out/ 4.8 GB | `du`, `git count-objects` |
| Big docs | 06-testing.md 7 898 lines / 688 KB (327 `##` sections), known-issues.md 3 619 lines / 272 KB (249 entries; tickets referenced: 484 fixed, 49 not-reproduced, 7 open), CHANGELOG 198 KB, REPORTING.md 503 lines (player part ~60) | wc, parse |

## Findings

### KIT-1 Release-only CI failures from environment drift (High)

* Evidence: tag builds failed in CI after passing locally:
  * v0.7.3.4 (run 41433238606): `sporekit/jpeg.cpp:232: unknown type name 'ptrdiff_t'` with libstdc++ 12 on
    ubuntu-22.04 (Fedora's headers include it transitively).
  * v0.7.11 (run 42519717067): `test_lint` could not parse 5 test scripts: f-strings valid on the lab's Python 3.14
    are SyntaxErrors on the runner's 3.10. Release 0.7.11.1 was the "release build fix".
  * v0.7.14 (run 43598488206): notes-check `BUG-1003, BUG-1041: named under Fixed but there is no ticket` (private
    tickets live in out/tickets, which a clean checkout lacks). The tag was then moved to a762f240 (tag date
    15:31 UTC, after the 13:11 UTC failure).
  * The lesson for Python is written down (docs/06-testing.md#testing-ci-python310) but nothing enforces it.
  * main and the tag are pushed together, so main's CI result is not known before the tag build starts (both
    failed together for 0.7.3.4 and 0.7.11).
* Bug link: BUG-0097, BUG-0165 (release checks), the 0.7.11.1 hotfix release.
* Proposal: (a) `actions/setup-python` pinned to the lab's Python minor version (or set the tools' floor to
  3.10 and run `uvx python@3.10` parsing in `make test`, which is weaker); (b) a `make release-dry` that runs
  `git archive HEAD` into a temp dir and runs `make all test`, `tools/release_check.sh` and `tools/package.sh`
  there, as CI does, with no out/ and no git metadata beyond what CI has; (c) release order: push main, wait for
  green (`gh run watch`), then push the tag; (d) never move a published tag (enable GitHub immutable releases and a
  tag ruleset; owner setting).
* Size: S. Risk: low. Verification: re-create the three failures on purpose in a scratch branch
  (`std::ptrdiff_t` include removed, a 3.12 f-string, a private BUG id in notes) and see `make release-dry` fail
  locally on each.

### KIT-2 The updater trusts the release host alone (Medium, security)

* Evidence: updater/winupdate.cpp:313-331 `download_pack`: downloads SHA256SUMS.txt and the pack from the same
  GitHub release and compares them; no signature, no key in the binary. installer/setup.cpp downloads the same
  way. The pack holds steam_api.dll, sporekit.dll and coop.dll (updater/pack.cpp:72-90 allowed paths), so whoever
  can publish a release (a stolen GitHub token or session, a compromised workflow) runs code on every player who
  clicks Update. The provenance attestations (docs/release-trust.md) are not checked by the client. Path safety of
  the pack itself is good (allowed_path / canonical_path, device names, trailing dots).
* Bug link: BUG-0784 (trust), BUG-0392 (web fallback also takes the release from the page redirect).
* Proposal: an Ed25519 signature over SHA256SUMS.txt (`SHA256SUMS.txt.sig`), public key compiled into coop.dll and
  the setup, verified before `sums_lookup`; a small vendored verifier (TweetNaCl-size, ~300 lines) or Windows CNG
  (ECDSA P-256 is available on Win7+, Ed25519 is not). Key custody is the real decision: an offline key held by the
  owner contradicts autonomous releases; a key in a GitHub secret protects only against asset tampering, not
  account compromise. Cheaper first step: GitHub immutable releases (assets cannot be replaced after publish).
* Size: M (S for the immutable-release setting). Risk: medium (a bad key path bricks the in-game update; keep the
  old check behind a switch for one release, test with the local release server of BUG-1015's tests).
  Verification: win_unit test with a tampered SUMS + valid hash pair must refuse; a good signature must pass.

### KIT-3 Player crash frames in coop.dll cannot be symbolized (Medium)

* Evidence: tools/package.sh strips every DLL; the release keeps no debug copy. tools/crash_triage.py:205-208 prints
  `llvm-symbolizer --obj=build/coop.dll ...`, the current dev build, "only right for the build of that version".
  57 files in out/reports and 9 tickets carry `coop.dll+0x...` frames. docs/06-testing.md:5913 says llvm-symbolizer
  is not installed, but it ships in the pinned llvm-mingw (`$LLVM_MINGW/bin/llvm-symbolizer`). docs/release-trust.md
  says two builds of one commit differ only in the timestamp and CodeView id, so the layout is reproducible.
* Bug link: triage skill step 5; BUG-0977, BUG-0850 (crashes analysed through coop offsets).
* Proposal: the release job uploads `SporeCoop-X.Y.Z-symbols.zip` (the unstripped DLLs, ~15-20 MB compressed) as a
  release asset; crash_triage.py takes the report's coop version, fetches and caches that zip and prints the
  function and line for every coop.dll / sporekit.dll frame. Optionally `-Wl,--no-insert-timestamp` (lld) for fully
  identical rebuilds as the fallback for old releases.
* Size: S-M. Risk: low (the asset is public source anyway). Verification: take a player report from 0.7.13 with a
  coop frame and check the symbolized function against the ticket's known root cause.

### KIT-4 No `-fno-strict-aliasing` / `-fno-delete-null-pointer-checks` for game-memory code (Medium)

* Evidence: Makefile:9 `COMMON := -O2 -g -Wall ...`; the code reads and writes game objects through casts at fixed
  offsets everywhere (sdk/include/sporekit_game.hpp:28 `field<T>`, mods/coop/cellsync.cpp:56 `F<T>`,
  `*(uint32_t*)(a + 8)` in npc.cpp:3341) and SporeKit repairs faults and lets code continue (exceptions.cpp, the
  "repaired fault" path in diag.cpp). Under strict aliasing clang may reorder or drop such accesses; with null-check
  deletion a check after a dereference may vanish. No crash has been traced to it yet, which is the point: such
  miscompiles show up as rare, unexplainable state.
* Bug link: none found; preventive. Precedent: game-mod and kernel code build with these flags.
* Proposal: add `-fno-strict-aliasing -fno-delete-null-pointer-checks` to COMMON (DLL builds only).
* Size: S. Risk: low (tiny code size / speed change). Verification: build, compare `.text` size, `lab.py run smoke`.

### KIT-5 Warnings as errors and printf format checking (Medium)

* Evidence: 21 warnings today, all trivial (metrics). `SkHost::log`, `out_printf`, `sk::logf` and
  `out_vappend` have no `format(printf)` attribute (sdk/include/sporekit.h:69,91; sporekit/core.h:32; clang's
  -Wmissing-format-attribute at commands.cpp:22, log.cpp:171). Experiment: with the attributes the whole tree has 0
  mismatches, so the guard costs nothing today and catches the 32-bit vararg crash class (`%s` with an int, `%d`
  with a uint64_t) from now on. mods/coop/trace.h:20-24 already uses the attribute.
* Bug link: none open; the class matters on 32-bit (BUG-0164 shows log formatting already bit once).
* Proposal: fix the 21 warnings (delete 17 dead counters or read them in a diag dump), add the format attributes,
  `-Werror` for `-Wall -Wformat=2 -Wimplicit-fallthrough`; then add `-Wshadow` (20 sites in coop) in a later step.
* Size: S. Risk: low (CI builds with the same pinned toolchain; host tests use clang 14 on CI vs 22 locally: keep
  -Werror off for host test builds or pin the host clang). Verification: `make` and CI green; a deliberate `%s`
  with an int fails the build.

### KIT-6 Static analysis is not used, and only a curated subset is worth it (Low-Medium)

* Evidence: no .clang-tidy, no cppcheck, nothing in CI; clang-tidy and run-clang-tidy are already in the pinned
  llvm-mingw (`~/.local/opt/llvm-mingw-20261006-.../bin/clang-tidy`, LLVM 23.1.3). Results in Metrics: the analyzer
  is clean on sporekit except deliberate code; on coop it mostly flags the raw-offset helpers. Useful real-looking
  hits: implicit `!wcscmp`/`memcmp` on floats (sporekit/gamehooks.cpp:249 compares floats with memcmp), 84 unchecked
  returns (cert-err33-c: fwrite/snprintf/fclose), 38 `atoi` without error checking, 3 empty catch blocks
  (sporekit/diag.cpp:464,732,737).
* Bug link: weak; the kit's tickets (BUG-1016..1035) are races and lifetime problems found by review.
* Proposal: a `.clang-tidy` with analyzer core/cplusplus/unix, bugprone-use-after-move, dangling-handle,
  sizeof-expression, suspicious-memset/string-compare, infinite-loop, integer-division, misplaced-widening-cast;
  `make tidy` (about 2 min for the tree with -P10); in CI as a non-blocking job with a checked-in baseline, failing
  only on new findings.
* Size: S. Risk: none (not a build dependency). Verification: the job runs in CI and a seeded use-after-move fails it.

### KIT-7 Report path masking misses non-ASCII Windows user names (Medium, privacy)

* Evidence: sporekit/log.cpp:62 `GetEnvironmentVariableA("USERPROFILE", ...)` returns the ANSI code page, while log
  and report text is UTF-8 (`narrow`, log.cpp:228); `same_path_char` (log.cpp:72) lowercases ASCII only. A profile
  such as `C:\Users\<Cyrillic name>` therefore does not match and stays in full in sporekit.log and report zips that
  players attach to public issues. Documents redirected outside the profile (another drive, OneDrive) are not
  masked either. mask_paths has no unit test (only game tests touch reports).
* Bug link: same class as BUG-0142, BUG-1011 (non-ASCII names and paths).
* Proposal: read USERPROFILE and WINEHOMEDIR with `GetEnvironmentVariableW` and `narrow()`; also mask the Documents
  folder from `SHGetKnownFolderPath(FOLDERID_Documents)`; move `mask_paths` into a host-testable unit and test
  Cyrillic and mixed-case paths.
* Size: S. Risk: low. Verification: a host unit test; a win_unit run with `USERPROFILE=C:\Users\<Cyrillic>`.

### KIT-8 Stale and contradicting GOG 2016 texts, frozen code still carried (Medium)

* Evidence:
  * installer/setup.cpp:259 shows "Game: ... GOG 2016 build (supported)" in the setup window.
  * tools/package.sh writes README-SporeCoop.txt with "until 0.7.x, GOG 2016" and "Tested: GOG 2016 and 2024".
  * docs/INSTALL.md:52 "0.7.x is the last version for the GOG 2016 build. From 0.8 ..." contradicts
    INSTALL.md:26,73,79 "no longer supported since 0.7.8"; INSTALL.md:102 still lists GOG 2016 as tested.
  * CLAUDE.md section 1 still names `overrides_922af7c7.sym` as the file "every game function ... we use" goes into.
  * Frozen code: 256 symbols missing for 922af7c7 reported by re_lint at every `make test`; 20 source/tool files
    mention the build (npc.cpp 47 times), data/symbols carries 260 KB of 2016 tables.
* Bug link: BUG-0108, BUG-0165 (texts out of date).
* Proposal: one text pass (setup string, package.sh README, INSTALL, CLAUDE.md) now; removing the 2016 build
  (symbols, frozen_off list, re_lint's dual-build mode, setup hash, npc.cpp branches) as its own branch, since the
  owner's decision says the files stay frozen "until they are removed".
* Size: S (texts), M (removal). Risk: low for texts; removal touches npc.cpp/tribe.cpp (coordinate with the coop
  auditor). Verification: grep for 922af7c7 / "2016" returns only CHANGELOG history; `make test`, smoke.

### KIT-9 405 worktrees, 436 branches, 108 GB (Medium, hygiene)

* Evidence: `git worktree list` 406 entries; 393 on branches merged into main, 0 with tracked changes; `du -sh
  ../SporeCoop-wt` 108 GB, of which 100 GB are the worktrees' build/ and out/; `git branch` 436, 428 merged; 8
  unmerged (dev/census, dev/lab-gate, dev/net-fuzz, dev/twins, fix/save-hygiene, fix/w1-net from 2026-10-25..27,
  fix/w48-d3d9, rel/0.7.3.6). No tool removes worktrees (tools/lab.py creates them). Also 3 root folders
  build-v051/build-v0725/build-v0731 (236 MB, ignored) and 125 MB of loose git objects.
* Bug link: BUG-0048 (`--branch` picked a wrong tree), BUG-0068 (paths from worktrees); every worktree is also a
  place a stale copy of a tool can run from.
* Proposal: `tools/wt_gc.py` (dry run by default): remove worktrees whose branch is merged, with a clean status,
  not locked, not the cwd of a running process (`/proc/*/cwd`), untouched for 2+ days; `git branch -d` for merged
  branches; `git worktree prune`; `git gc`. Optional `lab.py` hook: `--branch` worktrees are removed after a merged
  run. Decide per unmerged branch (keep, archive as tag, delete).
* Size: S. Risk: medium only if it deletes a tree an agent is using now (the 0.7.15 auditors): the cwd and mtime
  guards plus a dry run first. Verification: counts and `du` before/after; `lab.py run smoke --branch` still
  creates a tree.

### KIT-10 Docs grew append-only and lost their role (Low-Medium)

* Evidence: docs/06-testing.md 7 898 lines in 327 dated sections (the reference and the diary in one file);
  docs/known-issues.md claims "Bugs found and not fixed yet" but its 249 entries reference 484 fixed tickets and
  only 7 open ones (24 tickets are open); docs/REPORTING.md is a 60-line player guide followed by 440 lines of
  maintainer internals; docs/02-modkit.md has 2.1-2.7 plus 10 dated sections (SporeKit 0.3.4 ... 0.3.17) that a
  reader must merge mentally; 10 plan files (plan-0.7.5 ... plan-0.7.14) in docs/.
* Bug link: BUG-0165 (stale INSTALL / README); KIT-8 shows the cost.
* Proposal: known-issues.md generated from open tickets by `tools/tickets.py index` (the history moves to
  docs/archive/known-issues-2026.md); REPORTING.md split into the player guide and docs/report-internals.md;
  06-testing.md split by topic with an index (anchors kept, docs_lint checks links); plan files of released
  versions to docs/archive/plans/; a "SporeKit reference" rewrite of 02-modkit.md with the dated sections folded in.
* Size: M. Risk: low (docs_lint guards anchors). Verification: docs_lint green; known-issues lists exactly the
  open tickets.

### KIT-11 Ticket metadata is not validated, so bug statistics are unreliable (Low)

* Evidence: `stage` has 38 spellings (All 214 + all 16 + any; Civ 73 + civ 8 + Civilization 9; "Creature,Tribe",
  "Tribe/Civ", "none", "-", empty in 4). `tools/tickets.py check` passes. 155 `test` + 57 `tooling` tickets (19 %)
  are about the test tools themselves.
* Proposal: an enum for stage and kind in tickets.py (`check` fails on others), a one-time normalisation, multiple
  stages as a list.
* Size: S. Risk: none. Verification: `tickets.py check` fails on a ticket with `stage: civ`.

### KIT-12 CI supply chain and maintenance (Low)

* Evidence: .github/workflows/build.yml: actions pinned by major tag (`actions/checkout@v4`, `upload-artifact@v4`,
  `download-artifact@v4`, `attest-build-provenance@v2`), and the run shows "Node.js 20 is deprecated" for them;
  the llvm-mingw tarball is fetched with `curl` and no checksum; `pip install --user pillow` unpinned; the toolchain
  version lives twice (Makefile:2 path and build.yml `LLVM_MINGW_VERSION`).
* Proposal: pin actions by commit SHA with Dependabot for actions; `sha256sum -c` of the tarball (hash in the repo);
  `pillow==<ver>`; one `toolchain.env` read by both Makefile and workflow; bump to the Node 24 action majors.
* Size: S. Risk: low. Verification: CI green; a wrong tarball hash fails the job.

### KIT-13 Build system and onboarding nits (Low)

* Evidence: `make B=<dir>` fails: installer/setup.rc:3-4 hard-codes `../build/setup.ico` and `../build/setup.manifest`
  (llvm-rc: file not found); .c objects (MinHook, loader) are built without `-MMD`, so a changed MinHook header does
  not rebuild them; `make test` fails outside a git checkout (tools/docs_lint.py:25 `git ls-files`), e.g. from a
  release source archive; README "Building from source" names neither the llvm-mingw version (20261006), nor
  Pillow, nor clang++ with compiler-rt for the sanitizer tests, nor the Python version.
* Proposal: pass `-I$(B)` and reference `setup.ico` / `setup.manifest` by name; `-MMD -MP` for .c; docs_lint falls
  back to a file walk without git; a "Requirements" block in README (or a `tools/get-toolchain.sh` that downloads and
  checks the pinned llvm-mingw into ~/.local/opt).
* Size: S. Risk: none. Verification: `make B=/tmp/x all`; `make test` in a `git archive` copy.

### KIT-14 Small duplicated low-level code (Low)

* Evidence: SHA-256 twice (sporekit/sha256.cpp:25, updater/sha256.cpp:25, the same constant table); widen/narrow
  three times (sporekit/log.cpp:228, mods/coop/slotfs.cpp:21, updater/winupdate.cpp:51); the "scan my own stack
  from a local marker to StackBase" loop four times (sporekit/commands.cpp:536,559, sporekit/watchdog.cpp:242,
  mods/coop/coop.cpp:3293), each with its own filter. BUG-1028 (fault logger read past the stack's region) was a bug
  of exactly such a loop: a fix in one copy does not reach the others.
* Proposal: a header-only `common/` (strconv, sha256) shared by sporekit, updater, installer; one
  `sk::stack_return_addresses(filter)` in SporeKit exposed to coop through SkHost (size-checked ABI addition).
* Size: S. Risk: low. Verification: host unit test for the shared pieces; kit_* lab tests for the stack dumps.

### KIT-15 Windows-side unit tests run only under Wine (Low-Medium)

* Evidence: Makefile:203-206 `test-win` builds build/win_tests.exe (installer, slotfs, updater file handling,
  inifile), run only by the lab under Wine (tools/tests/win_unit.py). CI never runs it; real Windows file semantics
  (sharing violations, MoveFileEx on locked DLLs, Defender holding new files) differ from Wine's. Installer and
  updater bugs: 27 tickets (BUG-0089, 0094, 0139, 0150, 0243, 0851, 1019 ...).
* Proposal: a `windows-latest` CI job that downloads the Linux job's win_tests.exe artifact and runs it.
* Size: S. Risk: low (tests that assume Wine paths need a skip). Verification: the job runs; a seeded failure shows.

### KIT-16 Twin fix switches never retire (Low-Medium, cross-cutting with the coop auditor)

* Evidence: 779 COOP_FIX switches and 37 KitFix in sporekit; 152 COOP_FIX names appear in no test; 195 are for
  tickets below BUG-0300 (fixed in 0.7.3.x). Each keeps an old buggy path compiled in (example: the update path
  check `allowed_path_old`, updater/pack.cpp:65-70; autoupdate.cpp has three switches in 120 lines).
* Proposal: a retirement rule: a switch whose fix shipped 3+ releases ago and that no test uses as its "fails
  without the fix" side loses its old path; `fixes_tests` lists candidates. Keep the switch for fixes whose tests
  still use it.
* Size: M (mechanical, many files). Risk: medium (behaviour cannot be switched back in the field); do it in batches
  with smoke runs. Verification: switch count drops; smoke and nightly green.

## Not worth doing

* A blocking clang-tidy/cppcheck gate over all of coop: the raw game-offset style produces mostly false positives
  (null-pointer arithmetic in `field<T>`, padding, "garbage" values in type punning); only the curated, baseline-based
  job of KIT-6.
* ASan/UBSan builds of coop.dll / sporekit.dll inside the game under Wine: the runtimes exist in llvm-mingw
  (libclang_rt.asan_dynamic-i386.dll), but the game's heap (msvcr90) is not instrumented, so the main crash class
  (a game object freed and used later) stays invisible, and the deliberate type punning trips UBSan. At most a one-
  day experiment, not a pipeline.
* More host sanitizer work beyond KIT-7's mask_paths test: the portable code is already covered with "must fail
  without the fix" checks; the remaining code needs the game.
* Compile-time work, a CMake/Meson migration, precompiled headers: a 26 s clean build and 44 s `make test` are fine.
* Full bit-for-bit reproducible builds beyond `--no-insert-timestamp`: the release-trust doc already shows rebuilt
  binaries match apart from the timestamp and CodeView id.
* Removing the kit.test.* crash and hook commands from the shipped sporekit.dll: they are how the lab tests the real
  binary, and the console listens on 127.0.0.1 only (sporekit/console.cpp:640).
* Removing hot reload despite its 23 tickets: it is the 1 s dev loop instead of a 40 s cold start; its bugs are
  developer-only and are fixed.
* Rewriting or replacing the patched MinHook: it works and its patches are commented in place. (Do record its
  upstream commit and a patch file in third_party/minhook, part of KIT-12.)
* A self-hosted runner for game tests in CI (decided against for security, docs/10-dev-pipeline.md 10.7), Git LFS for
  8 MB of media, or rewriting git history.
* Code signing: an owner identity decision (docs/release-trust.md), not engineering work for 0.7.15.

## Open questions

1. KIT-2: who would hold an update-signing key, given that releases are made autonomously? If nobody offline, is
   GitHub immutable releases plus a CI-held key enough for the owner?
2. KIT-9: are any of the 393 merged-branch worktrees in use by the running 0.7.15 agents or lab runs, and should the
   8 unmerged branches be kept, archived as tags or deleted?
3. KIT-8: remove the GOG 2016 support code in 0.7.15, or only fix the texts?
4. KIT-1: which Python is the tools' floor: the CI's 3.10 or the lab's 3.14 (then CI moves to it)?
5. KIT-16: does the owner accept that fixes older than N releases lose their off switch?
6. Are crash-report symbols (KIT-3) acceptable as a public release asset, or should they stay a CI artifact
   (90-day retention) plus the rebuild-from-tag fallback?
