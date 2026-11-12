---
id: BUG-0860
title: drop_tasks_in destroys queued task functions while holding g_task_mu: a destructor that queues a task would deadlock (suspected)
status: fixed
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor 28536e76, 6e4f5eab
role: 
versions: 
fixed_in: unreleased
branch: fix/w48-net
commits: 2f9c631b
tests: unit (make test: validate_tests test_task_drop)
signature:
docs: docs/02-modkit.md#modkit-task-drop
related:
switch: kit.taskdrop
---

## Report
No report. A possible freeze class during a hot reload (`mod.reload`): suspected only.

## Root cause
Observed in code, impact inferred (confidence low): `sporekit/console.cpp:100-113` `drop_tasks_in` erases the queued
std::function objects of the unloading module while holding `g_task_mu` (a plain std::mutex). Destroying a task destroys
its captures; if a capture's destructor queues a task (run_on_main / run_outside_frame lock the same mutex), the thread
deadlocks on the non-recursive mutex. No capture of ours that does this is known.

## Fix idea
From the fork (jwhans0n/SporeCoopGA 28536e76, 6e4f5eab); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
They move the matching tasks into a local container while the lock is held and destroy that container after unlocking
(the count is returned as before).

## How to test
Unit / native fixture: queue a task whose captured object queues another task in its destructor, then call
drop_tasks_in for its code range. FAIL: the call never returns (timeout); PASS: it returns, the second task is queued.

## Fix
Confirmed on the code (console.cpp drop_tasks_in erased the tasks under g_task_mu). The removal is
`sk::drop_tasks` (sporekit/task_drop.h, portable): the matching tasks are moved out under the lock and destroyed after
it is released. No capture of ours that queues a task in its destructor is known: this closes the class.
Switch `kit.fix taskdrop 0`. Not ported from the fork: written on our code.

## Verification
* Unit (make test): validate_tests `test_task_drop` (a capture whose destructor queues a task; a probe mutex counts a
  lock taken while it is held, the deadlock without hanging the test): FAIL `validate_tests --nofix` ("locks taken
  while held 1", out/n1-unit/validate_nofix.txt), PASS in make test twice (out/n1-unit/validate_pass_1.txt,
  validate_pass_2.txt).
* Not run on a game (a hot reload with such a task needs a mod that makes one).

## History
- 2026-11-10: opened.
- 2026-11-12: fixed on fix/w48-net (0.7.13 N1): tasks destroyed outside the lock; FAIL/PASS unit runs.
