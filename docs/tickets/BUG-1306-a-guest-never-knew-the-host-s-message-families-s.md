---
id: BUG-1306
title: A guest's own City Hall and vehicle editors never opened at its Civ arrival: civ_arrival asked peer_families(0), which answers only on the host
status: fixed
kind: bug
stage: Civ
reported: 2026-11-19
source: lab
role: guest
versions: rel/0.7.16
fixed_in: unreleased
branch: fix/w57-own-editors
commits: 800a17e6 6b93ebd6 39aebbea
tests: civ_late_arrival, civ_late_arrival_families_off, civ_own_arrival, line_stages_editors
signature:
docs: docs/re/civ.md#civ-own-arrival, docs/06-testing.md#testing-own-editors-night1
related: BUG-1276, BUG-1290, BUG-1282, BUG-1283
switch: core.own_civ_families
---

## Report
First lab run of 0.7.16 (2026-11-19): civ_own_arrival (run 20261119-095414: "the guest's OWN City Hall editor opened
after its arrival in Civ (301 s after it; phase none why '')"), line_stages_editors (same run: "civ: the guest's own
editors at its arrival gave it designs on both games ({})") and civ_late_arrival (run 20261119-104136: "the first
arrival (no own creature) offers no own editor" with checked=0, and "late not in own mode (or the host keeps no
species line)"). The guest's `coop.civarrival` said `allowed=0` in all three.

## Root cause
✔ (code + logs) civ_arrival.cpp allowed() needs host_has_line(), which read the host's families with
`Session::peer_families(0, ...)`. That function answers only when this game is the host (`role_ != Role::Host` returns
false), so on every guest host_has_line() was false, allowed() false: neither the vote path (BUG-1276) nor the late
path (BUG-1290, "not in own mode (or the host keeps no species line)") ever made the editors due. The same class hit
the npc 1.2 gates of BUG-1282 / BUG-1283 (fix/w57-sync added `Session::host_families`, the guest's side).

## Fix
host_has_line() reads the host's WELCOME families through `Session::host_families` (the same function and hunk as
fix/w57-sync's BUG-1282 commit 618fbd6e, so the two branches merge cleanly). Switch `core.own_civ_families`; off: the
host-only query (no own editors on a guest, as in the first lab run).

## Verification
FAIL (rel/0.7.16 before the fix): rel/0.7.16 out/lab/20261119-095414 civ_own_arrival (`allowed=0`, phase none) and
line_stages_editors ("civ: the guest's own editors at its arrival gave it designs on both games ({})"),
20261119-104136 civ_late_arrival ("not in own mode (or the host keeps no species line)"). Twin on the merged tree:
fix/w57-own-editors worktree out/lab/20261119-130049 civ_late_arrival_families_off FAIL for that reason. PASS: fix/w57-own-editors worktree out/lab/20261119-130049
civ_own_arrival ("the guest's OWN City Hall editor opened after its arrival in Civ (22 s after it)", its later checks:
BUG-1323's purchase set-up and the save strip), civ_late_arrival (both own editors opened, both designs in the line on
both games), line_stages_editors ("civ: the guest's own editors at its arrival gave it designs on both games").

## History
- 2026-11-19: opened.
- 2026-11-19: fixed on fix/w57-own-editors, lab runs above.
