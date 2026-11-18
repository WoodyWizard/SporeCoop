---
id: BUG-1127
title: A13: tools/wt_gc.py lists merged worktrees and branches to delete
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: 0.7.15
branch: feat/w51-hygiene
commits: 1c075c0d
tests: make test (tools_selftest wtgc)
signature:
docs: docs/10-dev-pipeline.md#pipeline-wt-gc
related: BUG-0048, BUG-0068
---

## Report
A13 of docs/plan-0.7.15.md (KIT-9): about 400 worktrees (393 merged, ~100 GB of build/ and out/) and 430+
branches; no tool removes them.

## Root cause
Every agent and `lab.py run --branch` makes a worktree; nothing removes them.

## Fix
tools/wt_gc.py: dry run by default, `--apply` deletes (`git worktree remove` without --force, `git branch -d` from
the main checkout, `git worktree prune`). A tree goes only when merged into main, not main / rel/* / a --keep glob /
detached / locked, `git status` empty, no merge / rebase / bisect in progress, no process uses it (cwd, PWD, open
file, command-line path), no held lab lane lock or game console port of a process started from it, and unchanged for
--min-age hours (48). Prints the build/ and out/ bytes it frees; merged branches without a tree go too; unmerged ones
are listed. Tooling, no switch.

## Verification
* tools_selftest wtgc (temporary repository): of 7 worktrees only the merged, clean, unused, old one is planned;
  uncommitted work, an unmerged branch, a tree with a process in it, a tree whose process holds a lane lock, a --keep
  glob and rel/* stay; a tree changed in the last 48 h stays; the dry run changes nothing; --apply removes the tree and
  the two merged branches and nothing else. Every case checked as both outcomes (FAIL lines for a wrong plan, PASS).
* Dry run on the real repository, 2026-11-15, `--keep '*/w51-*'` (never --apply): 307 worktrees to remove, 77.5 GB of
  build/ and out/, 342 merged branches; 108 trees kept (68 changed in the last 48 h, 16 rel/*, 8 unmerged, 6
  detached, 6 */w51-*, 3 in use by a process, 1 with uncommitted work); 16 unmerged branches for a person.

## History
- 2026-11-15: opened.
- 2026-11-15: done on feat/w51-hygiene (1c075c0d); only the dry run was run.
- 2026-11-18: released in 0.7.15.
