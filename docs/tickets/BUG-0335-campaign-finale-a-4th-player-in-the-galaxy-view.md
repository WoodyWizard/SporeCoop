---
id: BUG-0335
title: Campaign finale: a 4th player in the galaxy view enters the core with its first Fly to Alice; the bot's second step gets stuck
status: fixed
kind: bug
stage: Space
reported: 2026-10-31
source: lab 20261031-000328 campaign#3 (lane 7), run dir out/lane7/campaign/20261031-000341
role: guest
versions: main 0b76599
fixed_in: 0.7.3.2
branch: fix/w18-space
commits: 06419da
tests: campaign (finale), coop_campaign.py --finale-from-galaxy
signature:
docs: docs/06-testing.md#testing-campaign-finale-galaxy
related:
---

## Report
Final verification campaign (lab 20261031-000328, campaign#3 on lane 7, 4 players, run dir
out/lane7/campaign/20261031-000341) failed in space/finale with two [stuck] findings for the 4th
player: "01:24:10 finale: Dave: zoomed out to the galaxy view" and "01:25:34 Dave: 'Fly to Alice' on
the Space page (the gate)". The next check passed: every player entered the galactic core and got
the Staff of Life (01:25:43, 01:26:27). Dave's screenshot at the first finding shows the core
cinematic ("You are now to be given the power").

## Root cause
Observed (Dave's game log, campaign#3.extra2.game.log): Dave was in the galaxy view (world `-`)
when the bot pressed its first "Fly to Alice" (step "near the core", 02:22:26 game clock). From the
galaxy view that button flies straight to the core's gate (designed, docs/re/space.md 16.4). Alice,
Bob and Carol were already waiting at the gate, so the core opened at once: "Alice/Bob/Carol enters
the galactic core: together!" (02:22:27), Dave "entered the galactic core" and got Steve's gift
(02:22:31). Dave was then inside the core cinematic (input lock 1), while the bot went on with its
second step for a guest that starts in a system: zoom out to the galaxy view (impossible under the
cinematic: stuck) and "Fly to Alice" again (no such button once the core is reached: stuck).
Not a mod bug: the mod did what a player gets (a fourth player flying from the galaxy view joins
the waiting group and everybody enters together). A test step issue: the bot assumed every guest
starts in a solar system. Why Dave played on in the galaxy view: the Space play phase before the
finale leaves players wherever their bots were.

## Fix
`coop_campaign.py` finale: before each step the bot reads the guest's `coop.space finale` line; a
guest already in the core (or at the gate for the second step) skips the rest, a guest in the galaxy
view does only the gate step. `--finale-from-galaxy` puts every guest into the galaxy view before
its steps (reproduces the case); `CAMPAIGN_FINALE_OLD=1` runs the old steps (tests). No mod change.

## Verification
Replay of the finale alone, 4 players (direct runs of `coop_campaign.py --from space --to space
--players 4 --no-rejoins`, run dirs in the worktree's out/):

* FAIL with the old steps (`CAMPAIGN_FINALE_OLD=1 --finale-from-galaxy`, lane 8):
  out/lane8/campaign/20261031-020044: "[stuck] finale: Dave: zoomed out to the galaxy view" and
  "Fly to Alice ... (the gate)" (Dave inside the core cinematic, entered=1), as in the report;
  Bob and Carol stuck on their second step too. The original run: lab 20261031-000328.
* PASS with the new steps: out/lane9/campaign/20261031-020044 (`--finale-from-galaxy`, lane 9) and
  out/lane8/campaign/20261031-021210 (same, lane 8): every guest "in the galaxy view: Fly to Alice
  goes to the gate", all four entered and got the Staff of Life; out/lane9/campaign/20261031-021210
  (no flag, guests where the Space play left them): RESULT OK.

## History
- 2026-10-31: opened.
- 2026-10-31 (fix/w18-space): test step issue, not a mod bug; the finale bot reads the finale state before each step. FAIL out/lane8/campaign/20261031-020044, PASS 3 runs.
- 2026-11-01: released in 0.7.3.2.
