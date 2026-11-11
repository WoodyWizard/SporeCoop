---
id: BUG-0860
title: drop_tasks_in destroys queued task functions while holding g_task_mu: a destructor that queues a task would deadlock (suspected)
status: open
kind: bug
stage: All
reported: 2026-11-10
source: fork jwhans0n/SporeCoopGA sync/upstream-0.7.8-editor 28536e76, 6e4f5eab
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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
## Verification

## History
- 2026-11-10: opened.
