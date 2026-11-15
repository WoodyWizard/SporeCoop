---
id: BUG-0991
title: tests: campaign finale stuck: the host's ship was destroyed (the space bot flew at 8 of 1500 hp) and the finale's take-off zoomed out under the death caption
status: fixed
kind: test
stage: Space
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 campaign, out/lane2/campaign/20261112-175451
role: 
versions: 0.7.13-rc
fixed_in: 0.7.14
branch: fix/w49-space
commits: f95e96fe
tests: space_finale_death, campaign (finale)
signature:
docs: docs/06-testing.md#testing-finale-death
related: BUG-0336
---

## Report
rel/0.7.13 final check, campaign (out/lab/20261112-175447/campaign.log, findings
out/lane2/campaign/20261112-175451/findings.md finding 3): `[stuck] space/finale: finale: Alice: zoomed out to
the galaxy view`. The campaign then went on through its shortcut (`coop.space finale goto near / core`) and the
finale ended ok (the host waited at the core, the guests flew there).

## Root cause
Observed (host log, trace lines): the host's ship flew at 8 of 1500 hp from 19:10 on (the space bot does not
repair), 1/1500 at 19:11:34, destroyed at 19:11:54 (hp 0/1500, input_lock=1, back at the homeworld 80AE3044);
the screenshot campaign_03_stuck_Alice.png shows the game's caption "Your <ship> has taken so much damage that it
has been destroyed ... you return to your homeworld, a new spaceship at the ready" with its green check button.
The finale's take_off (coop_campaign.py) zooms out 10 x 5 notches while that caption holds the input: nothing
moves. Its settle (coop_space_finale.settle) only presses captions while the finale state says busy=1; this
caption did not set it (inferred from the 10 rounds without a press).
Verdict: the bot (the space bot's ship left near death, the finale's settle blind to the death caption), not the
mod: the destruction is the game's (enemies in the host's system), and the mod's finale went on normally.

## Fix
Test bots only (tools/tests/coop_space_finale.py, coop_campaign.py); no switch in the mod, the old bot behaviour is
`OLD_FINALE_DEATH=1`:
* death_caption(con): `coop.space hp` shows the death caption (cine=1) or a destroyed ship (state=2); settle() treats
  it as busy and presses the caption's OK (Enter / the green check, 6 s after it came up, as for any caption).
* to_galaxy(): before each wheel, a death caption is confirmed first; then it waits for the respawn (state 0, no
  cinematic), puts the drive back when the new ship lacks it, and gives the wheel 25 more tries (the wheel did
  nothing for about 20 s after the new ship arrived, lab 20261113-020049-2).
* The campaign's finale repairs a ship below half its hp first (`coop.space heal`, a new test command that sets the
  ship's hp to its max: the bots do not repair, and the host flew at 8 of 1500 hp), noted as a shortcut.

## Verification
`coop_space_finale_death.py` (suite `space_finale_death`, fixture space_free, ~2.5 min): the guest travels alone to
another star, takes a lethal hit (the death caption is up), then the finale's to_galaxy as the campaign runs it.
* FAIL (OLD_FINALE_DEATH=1): out/lab/20261113-020351-2 (stuck: "guest: zoomed out to the galaxy view" fails, as
  in the 0.7.13 campaign)
* PASS: out/lab/20261113-020351, out/lab/20261113-020718-2 (the caption confirmed, the ship respawned, the galaxy view reached)
Not run: a full campaign (1.5 h); the steps are the ones its finale calls.
Found on the way: on the `space` fixture (tutorial lock on) the respawned ship stayed in the system view; a new ship
keeps the drive the bots added (`tool add` said "already there").

## History
- 2026-11-12: opened.
- 2026-11-13: fixed on fix/w49-space: the finale bot confirms the death caption and repairs before the finale; twin test.
- 2026-11-14: released in 0.7.14.
