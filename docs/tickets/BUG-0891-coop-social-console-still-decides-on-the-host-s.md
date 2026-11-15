---
id: BUG-0891
title: coop_social_console still decides on the host's acts_in counter, not on what the games play
status: fixed
kind: test
stage: Creature
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: 0.7.13
branch: fix/w48-small
commits: b78132b3 3156086b
tests: social_console
signature:
docs: docs/06-testing.md#testing-p1-g1
related: BUG-0149, BUG-0215, BUG-0713
---

## Report
BUG-0215's own Fix section left this open ("coop_social_console.py still decides on the mod counter acts_in") and it was never ticketed. The test of BUG-0149 (digits typed in the cheat console must not play social abilities) checks only that the host's mod counter `acts_in` did not change. A regression that plays the ability on the guest's own avatar without sending K_ACT, or a host that counts the act but plays nothing on the puppet, passes or fails for the wrong reason. Since BUG-0713 the keys come from the game window's message queue and only the console check (`C.blocked`) stops them, so this is the only guard and it has no game-state assertion.

Severity: low. Confidence: high.

## Root cause
* Observed (code): tools/tests/coop_social_console.py:75-76 (`acts()` reads `acts_in=` of the host's `coop.social`), :91 `ck(a1 == a0, ...)`, :104 `ck(a3 > a2, ...)`; nothing reads the guest avatar's or the host puppet's animation or social state.
* Observed (code): mods/coop/social.cpp:666-692, the key path is gated only by `on` (engaged, social stance, `C.blocked`, front window).

## Fix idea
Assert the game's state on both machines: the guest avatar's current animation is not a social ability animation while the console is open and is one after it closes; the host's puppet of the guest plays the same animation (or the NPC's social reaction changes) only after the console closes. Keep acts_in as printed diagnostics only.

## How to test
With `coop.social textfix off` the new animation assertion must FAIL (the avatar plays the ability while the console is open); with the fix it must PASS. Both run directories go in the ticket.

## Fix
No mod change for players (a test fix). social_console now judges what the games play: `coop.npc cur` also prints the own avatar (`cur avatar`) and every partner's puppet (`cur p<id>`) with the game's current animation (vt 0x58); `coop.social abil` prints each ability's animation (`anim=`, cCreatureAbility::GetAnimationID(0)). The test samples the guest's avatar and the host's puppet of it every 0.1 s while the cheat console is open and after it closed: none of the social abilities' animations may play while it is open, one must play on both games after it closed (the press is repeated while the ability cools down). acts_in is printed only. Twin: SPORE_FIX_OFF=social.text (= `coop.social textfix off`, `--without-fix`).

## Verification
* FAIL with SPORE_FIX_OFF=social.text: out/lab/20261112-123648 (the social ability's animation 042A1CAF played on the guest's avatar and on the host's puppet while the console was open).
* PASS: out/lab/20261112-123811 (042A1CAF only after the close, on both games), out/lab/20261112-123930.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0215. Severity low, confidence high.
- 2026-11-12 (fix/w48-small): fixed: the test asserts the games' animations; FAIL / PASS runs.
- 2026-11-12: released in 0.7.13.
