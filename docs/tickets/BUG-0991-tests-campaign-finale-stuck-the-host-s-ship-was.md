---
id: BUG-0991
title: tests: campaign finale stuck: the host's ship was destroyed (the space bot flew at 8 of 1500 hp) and the finale's take-off zoomed out under the death caption
status: open
kind: test
stage: Space
reported: 2026-11-12
source: rel/0.7.13 final check out/lab/20261112-175447 campaign, out/lane2/campaign/20261112-175451
role: 
versions: 0.7.13-rc
fixed_in:
branch:
commits:
tests: campaign (finale)
signature:
docs:
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
Not done (a campaign run is 1.5 h; cheap only with a run): settle could press the caption's check while the
input is locked (`coop.status` input_lock) and the space bot could repair under 20 % hp.

## Verification
no-fail-run: open (bot); seen once

## History
- 2026-11-12: opened.
