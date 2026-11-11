---
id: BUG-0869
title: Feature idea: warm reconnect reuses the unchanged Cell world instead of a new world transfer
status: open
kind: feature
stage: Cell
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages30-dev (world.cpp resume receipt, CAP_RESUME_WORLD)
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
A guest that drops for a moment (Wi-Fi, a game restart into the same save) downloads the whole world again
(2-4 MB, minutes on slow links). The fork reuses the guest's already loaded, unchanged Cell world.

## Root cause
Not a bug, an optimisation of the reconnect path (world.cpp share on every new join).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages30-dev, mods/coop/world.cpp queue_slot / finish / guest_tick / resume_world, net/session.cpp CAP_RESUME_WORLD); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Host: every Cell world share gets a random 64-bit receipt (QueryPerformanceCounter, a serial, the process id),
  appended to MSG_WORLD_DONE; the host remembers the last 32 {receipt, slot, world id, Cell stage epoch, newest
  write time of the slot, shared food total}.
* Guest: after loading that world it keeps the receipt with its own world id, epoch and food; on reconnect HELLO
  carries CAP_RESUME_WORLD (64) + the u64 receipt, only while it is still in that Cell world.
* Host: accept the reuse only when the receipt is known, the slot has no newer write (no save since), same stage
  and world, else the normal share. Save changes, stage changes and cold reconnects use the normal path.
* Overlap: none in plan-0.7.12; fits the 0.7.4 lifecycle / replicated state layers. Extend to other stages later.

## How to test
Lab, 2 instances, Cell: guest joins (full transfer), drop its network for 10 s (lab link off), reconnect: no
MSG_WORLD_OFFER, the guest keeps its pool, partner cells back within 2 s. Control: the host saves during the drop:
a full transfer happens.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
