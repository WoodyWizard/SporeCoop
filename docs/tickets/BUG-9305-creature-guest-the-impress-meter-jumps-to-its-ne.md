---
id: BUG-9305
title: Creature (guest): the impress meter jumps to its new value instead of filling smoothly
status: open
kind: bug
stage: Creature
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch: fix/l47-impress
commits:
tests:
signature:
docs: docs/re/creature.md#creature-meter-native-fill
related: BUG-9297
---

## Report
Live 67-dev, Creature, a guest (owner, 2026-11-20): "Even when it was the correct one, it snapped the fill, rather than
gradually filling like native Spore."

## Root cause
Observed in the code, with the native fill read from the public spore-decomp (docs/re/creature.md#creature-meter-native-fill;
not seen at runtime by us). The native meter: an answer pushes a step entry into the bar's list at once (StartFill), and
the bar then moves linearly over the answer's animation (SocialValues::Update: SetBar(i, elapsed / length)); a bar
draws only when its list has entries. The guest's meter (BUG-9230 meter_fill + the BUG-9258 glide) had three snaps:
1. meter_fill wrote a round's entry only once the fill passed half the round (`lround(frac * rounds)`): the first half
   of a round drew nothing, then the bar appeared at once (in a 1-round game: nothing until 50 %).
2. The glide moved 20 % of the rest every 30 ms: half the step in about 0.1 s at 60 fps, an eased jump.
3. The win: K_RESULT comes right behind the last K_PROGRESS; the guest set the meter full at once (op 2) and closed it
   the next frame (g_engaged = -1). "Even when it was the correct one" fits 3 (a winning answer).

## Fix
Done (fix/l47-impress, switch `social.meter_smooth`, `coop.fix social.meter_smooth off` = the 67-dev glide):
the round being filled has its entry from its start (ceil), the bar moves linearly to the host's new value over
kFillTime = 1.0 s (every frame), the win waits for the fill to reach the end (kWinWait 3 s at most) before Finish and
the win message, and the full meter stays kWinHold = 1.5 s before it closes. `coop.social meter` prints `entries=a/b
goal= glide_from= win_wait= wins_deferred= smooth=`. Inferred: 1.0 s per answer approximates the native answer
animation's length (not measured).

## Verification
no-fail-run: pending the lead's two-player QA (built only; a visual check plus `coop.social meter` samples).
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): partial: right after the first matched answer the guest meter showed entries=1/1 goal=0.25 frac_set=0.25, 0.37 after the second (samples every ~1.2 s over the tunnel, too slow to see the 1 s glide). No visual check, no off-run.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-impress (built, not run in game).
- 2026-11-21: live two-game QA (see Verification).
