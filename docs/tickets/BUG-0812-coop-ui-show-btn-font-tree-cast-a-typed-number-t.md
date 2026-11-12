---
id: BUG-0812
title: coop.ui show/btn/font/tree cast a typed number to a window pointer: a control id crashes the game
status: fixed
kind: crash
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 BUG-9108 (commit 6c8c4b2, lineages42-dev)
role: host guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-small
commits: 7e576f89
tests: ui_bad_arg
signature: access violation in sui set_flag / set_visible (virtual call through a control id such as 0xC00F0100)
docs: docs/06-testing.md#testing-p1-g1
related:
switch: core.ui_window_arg
---

## Report
Developer console / test drivers only: `coop.ui show <hex>` given a control id instead of a window address
crashed the game (fork QA run). Players do not type it, but our UI tests and agents do.

## Root cause
Observed in our code (confidence high): `mods/coop/sui.cpp:573`, `:590`, `:594` and `:599` cast
`strtoul(argv[...], 16)` straight to `Win` and call set_font / set_visible / tree on it. Any wrong number is a
wild pointer dereference.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages BUG-9108, commit 6c8c4b2); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* One helper `arg_win(text)` in sui.cpp: parse the number, then accept it only when a window with that address is
  in the live UI tree (walk from main_window), or else when a window with that control id is found in the tree;
  otherwise print "no window ..." and return 1. Use it for show, btn, font and the tree root.

## How to test
Unit/console test on one instance: `coop.ui show 06318E78 1` (a control id) and `coop.ui show 12345678 1`.
FAIL before: game fault. PASS after: the first resolves by control id (or prints "no window"), the second prints
"no window", zero faults; `coop.ui tree` addresses still work.

## Fix
Root cause confirmed (the number went straight into a vtable call). sui.cpp `arg_win`: a number is a window only when a window of the live UI tree (from main_window) has that address; else the first window of the tree with that control id; else the command answers "no window ..." and returns 1. Used by `coop.ui show`, `btn`, `font`, `caption`, `area`, `align` and the tree's root. `coop.fix core.ui_window_arg off`: the number as it is (before).

## Verification
ui_bad_arg (`coop_ui_bad_arg.py`, one game at the galaxy menu): a window address still works, its control id is that window, numbers no window of the tree has answer "no window" for each command, no fault, frames go on. (The menu has a window with control id 00000010: the first PASS attempt, out/lab/20261112-122347, took it for a wrong number; the test now uses only numbers absent from the tree.)
* FAIL with SPORE_FIX_OFF=core.ui_window_arg: out/lab/20261112-122317 (the game crashed at `coop.ui tree 0 all FFFFFFF0`, a control id taken as a window: crash report crash-20261112-122334-i5).
* PASS: out/lab/20261112-124304, out/lab/20261112-124328.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (code audit).
- 2026-11-12 (fix/w48-small): fixed (core.ui_window_arg), test ui_bad_arg.
