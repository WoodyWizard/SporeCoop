---
id: BUG-1113
title: A16: a second headless Xwayland for the lab with its own runtime folder; lanes spread over the displays
status: fixed
kind: tooling
stage: lab
reported: 2026-11-15
source: plan
role: 
versions: 
fixed_in: unreleased
branch: feat/w51-labstart
commits: ee9d9990 ff5df396 7fdc676c 6c35515c 06a43616
tests: labstart_selftest.py (displays), tools_selftest.py headless
signature:
docs: docs/06-testing.md#testing-two-displays
related: BUG-0663 BUG-1098 BUG-1114
---

## Report
Plan 0.7.15 A16 (owner decision 12; TST-3, BUG-0663): every lane's games drew through the one Xwayland of the
headless shell, which saturates at 10-14 games.

## Root cause
One shared headless GNOME Shell for every lane.

## Fix
tools/headless.sh keeps SPORE_HEADLESS_DISPLAYS shells (default 2), each with its own runtime folder under
$XDG_RUNTIME_DIR (config, state, cache, D-Bus session, Wayland socket, Xwayland auth): the global rule that a separate
headless shell needs its own runtime folder or it breaks the Flatpak document portal. Lane N draws on shell
(N - 1) % displays; `headless.sh name` / `list`; spore.sh passes its lane. labbudget.display_name has the same rule;
lab.py samples the lane's own Xwayland (results.json `xwayland.display`). SPORE_HEADLESS_DISPLAYS=1: the one shell of
before.

## Verification
no-fail-run: lab infrastructure. The second shell came up and down 3 times with the document portal mounted
throughout; VRAM 0.5-1.4 GB per extra shell (8-12 games running), so the card holds about 16 games with both (the
budget of BUG-1114 reads the VRAM). In the A/B of BUG-1112 the shared Xwayland peaked at 70-88 % of a core per test
(lab 20261115-014126, old code, lane 5) while the second one served lane 4 at 4-5 % (20261115-014106). Every new-code
run on lane 4 (20261115-014106, -020357, -021524, -022027, 20261115-022517) drew on sporecoop-headless-2. Unit:
labstart_selftest (headless.sh name == labbudget.display_name for lanes 1-10, 1-3 shells).

## History
- 2026-11-15: opened.
- 2026-11-15: fixed on feat/w51-labstart.
