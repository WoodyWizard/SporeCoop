---
id: BUG-0043
title: an evolution or superweapon vote still runs after its stage or session is gone
status: fixed
kind: crash
stage: All
reported: 2026-10-27
source: code review round 1 of main 563f72d, GitHub WoodyWizard/SporeCoop#21
role: both
versions: 0.7.2.4 (main 563f72d) and earlier
fixed_in: 0.7.3.0
branch: fix/w1-core
commits: 066df15
tests: tools/tests/coop_vote_stale.py (suite vote_stale)
signature:
docs:
related: #21
switch: core.vote_scope
---

## Report
GitHub issue: https://github.com/WoodyWizard/SporeCoop/issues/21 (full text, discussion).

A vote (evolution / design editor, or the civ superweapon) keeps no record of the stage, world or session it was started in, and no session-end path resets it. When the host leaves the stage during the vote, or stops hosting and hosts again, the old vote finishes later and runs its stored action. For the superweapon that calls `o_FireSW` with a civilization pointer from the old planet; for the editor it submits an EditorRequest with the old species key. Severity: likely crash (superweapon on a freed `cCivilization`), wrong behaviour (an editor opening out of nowhere on re-host).

### Where
- `start_action_vote` / `start_vote`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L433-L464
- `finish_vote` (runs the action outside the frame, no stage check): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L730-L752
- `vote_tick` (only while hosting, finishes on "alone" / "time is up"): https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L766-L784
- The superweapon votes capture a raw `s_civ`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/civ.cpp#L697-L731
- `submit_tick` only checks `stage_mode(game::mode_id())`: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L627-L648
- `do_leave` and the disconnected callback leave `g_vote` as it is: https://github.com/WoodyWizard/SporeCoop/blob/563f72d98cce2acf2c44b859f1dfe32f9588d401/mods/coop/coop.cpp#L1761-L1771

### How to reproduce
Case A:
1. Two instances, `civ_npc` fixture, host plus 1 guest.
2. On the host, fire a superweapon (or `coop.civ` with a superweapon command, if the test hooks have one). The vote shows "wants to fire a superweapon".
3. On the host: `coop.menu` (save and leave for the galaxy menu).
4. Wait 60 s, or press "I'm ready" on the guest. Watch the host for a fault in FireSW / an access violation.

Case B:
1. Two instances, `creature` fixture. On the host: `coop.vote test` (or open the editor at the nest) so a vote is active.
2. On the host: `coop.leave`, then `coop.host`.
3. On the first frame the feed shows "evolution starts (alone)" or "(time is up)". With a real editor request, the editor opens without any action of the player.

### Expected
A vote ends when its stage or world is left, and when the session ends. Its action or request is dropped then, not run later.

## Root cause
**What happens**

Case A, the civ superweapon, host leaves the stage during the vote:
1. The host fires a superweapon with a guest present. `h_FireSW` stores `s_civ = civ` (static, raw) and calls `C.vote(..., [has_target] { o_FireSW(s_civ, s_type, ...); })`.
2. `start_action_vote` keeps the lambda in `g_vote_action`; `Vote` has no mode or world.
3. The host presses Esc, "Exit to main menu" (or loads another save) before everybody is ready. The planet and its civilizations are unloaded.
4. The host is still hosting, so `vote_tick` keeps running. Within 60 s either the guest presses "I'm ready" or `left <= 0`, and `finish_vote("time is up")` posts `run()` with `run_outside_frame`.
5. `o_FireSW(s_civ, ...)` runs in the galaxy menu on the freed civilization.

Case B, re-hosting:
1. During any vote the host presses "Stop hosting" (`do_leave`). `g_vote.active` stays true, and `g_vote.request` keeps its reference or `g_vote_action` its lambda.
2. `vote_tick` returns early while not hosting, so the vote neither finishes nor goes away.
3. The host hosts again (same or other save, maybe minutes later). On the first frame, `vote_tick` sees `g_vote.active`. `g_ready` was cleared by `reset_lobby`, so `all` is false. `left = 60 - (now - t0)` is negative, or the host is alone, so `finish_vote` runs:
   - an action vote fires its action (Case A again, now possibly in another stage);
   - an editor vote calls `hold_submit(req)`. `submit_tick` only checks that some stage is played, so the editor opens for the old species key, possibly in another save.
4. Meanwhile, every real editor request in the new session hits `if (!g_vote.active) start_vote(...)` in `h_Submit`, is silently dropped and returns false, until the stale vote is gone.

Guest side: a guest that disconnects during a vote keeps `g_vote.active`. On the next session `started = on && !g_vote.active` is false, so its first vote neither opens the co-op window nor clears its own ready flag.

### Confidence
CONFIRMED by reading the full path for both cases. Not verified: whether `cCivilization` is freed on "Exit to main menu" (very likely, as the planet is unloaded), and what the game does with an EditorRequest of a species not in the loaded save.

## Fix
Fixed on fix/w1-core (066df15). `Vote` keeps the mode and world it was started in. `vote_tick`
ends a vote whose stage or world is gone ("cancelled (the stage was left)") through `drop_vote`:
the editor request is released (`vcall(req, 2)`), the action is not run, the co-op window closes if
the vote opened it. `end_session_modules()` (every session end) calls `drop_vote` too, so a new
session starts without a vote (host and guest). Old behaviour: `coop.corefix vote_scope 0`.
Not done: the extra check in civ.cpp's superweapon lambda (`player_civ() == s_civ`), area D; with
the vote scoped the lambda no longer runs after the planet was left.

The review suggested:

- Store `mode`, `current_world()` and a session counter in `Vote` at `start_vote` / `start_action_vote`.
- In `vote_tick`: when the mode or world differs, or the session is not the same, end the vote with "the stage was left". Drop `g_vote_action` and release `g_vote.request` (`vcall(req, 2)`) without running them.
- Reset `g_vote`, `g_vote_action`, `g_vote_opened_ui` in the session-end helper (see the umbrella issue on session-end paths), releasing the request.
- In civ.cpp, check in the lambda that `player_civ() == s_civ` and that the civ is still alive before `o_FireSW`.
- Keep the old behaviour behind `coop.fix vote_scope off`.

## Verification
Two-game test `coop_vote_stale.py` (creature fixture, lane 2, 2026-10-28):
1. `coop.vote editor` (a real EditorRequest through the vote), `coop.leave`, `coop.host`: the host's
   game mode must stay the stage. 2. `coop.vote action` (an action vote like the superweapon's),
   `coop.menu` to the galaxy menu, the guest clicks "I'm ready": the action must not run.
FAIL with `--without-fix` (vote_scope 0): the host's mode became `Editor` after hosting again, and
the action ran in the galaxy menu (actions run 0 -> 1). PASS with the fix, 2 runs.

The planned test was:

`tools/tests/coop_vote_stale.py` (two instances):
1. Start an action vote in civ (superweapon) on the host, then `coop.menu` on the host. Wait 65 s. Assert the host game is alive (lab crash check), `coop.vote` shows no active vote, and no superweapon effect or money change happened on the guest's machine (game state, not a mod counter).
2. `coop.vote test`, `coop.leave`, `coop.host` on the host. Assert that no editor opens (`game.mode` stays the stage) and that a following real editor request starts a new vote on the guest (the guest's window shows the vote text).

It must fail on main and with `coop.fix vote_scope off`.

### Related
- Umbrella issue on session-end paths (the vote is one of the states no path resets).
- `tools/tests/coop_vote.py`, `coop_civ_play.py`.

Run evidence (2026-10-28, BUG-0215, found afterwards in the lab folders and agent logs): FAIL with the fix off: `vote_stale_nofix.log` (fix/w1-core, vote_scope 0), a standalone test run whose log stayed in the agent's scratch folder (not kept in out/lab). PASS: lab run out/lab/20261027-192300-fix-w1-core on fix/w1-core (vote_stale ok); later full runs on main pass too.

## History
- 2026-10-27: found in code review round 1 of main 563f72d, filed as GitHub #21.
- 2026-10-27: ticket opened from the issue.
- 2026-10-28: fixed on fix/w1-core: votes scoped to mode, world and session (coop_vote_stale.py).
- 2026-10-28: released in 0.7.3.0.
- 2026-10-28: Verification names the FAIL log and the PASS lab run (BUG-0215).
- 2026-10-28: switch: core.vote_scope (the coop.fix registry names; CORRECTION: a switch name in the text above that is not one of these, such as `session_end`, is from before the registry and `coop.fix` answers "unknown fix" to it) (BUG-0214).
