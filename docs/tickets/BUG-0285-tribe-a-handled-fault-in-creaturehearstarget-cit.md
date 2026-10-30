---
id: BUG-0285
title: Tribe: a handled fault in CreatureHearsTarget (citizen WalkTo) while the stage is left for the galaxy menu
status: open
kind: crash
stage: Tribe
reported: 2026-10-29
source: lab campaign 20261029-174717 (4 players, lane 7)
role: host
versions: 0.7.3.0 (main b5ef74d)
fixed_in:
branch:
commits:
tests:
signature: 0xC0000005 at 0x00C0DB23 SporeCoop::CreatureHearsTarget+0x463 data 0x3C (handled, first chance)
docs:
related: BUG-0284
---

## Report
The 4-player campaign (lane 7, GOG 2024, host) left the Tribe stage for the galaxy menu (the jump
to the civ_npc fixture: `coop.load`, quit message posted). 0.86 s after the quit, in the frame the
mode changed, the exception logger caught a first-chance fault, handled by the game itself:
`0xC0000005 at 0x00C0DB23 SporeCoop::CreatureHearsTarget+0x463 (data 0x0000003C)`, ebx=ecx=edx=0,
called from `cCreatureBase::WalkTo+0x738 < WalkTo+0x7D0 < cCreatureCitizen::DoAction+0x496`.
Return addresses on the stack (GOG 2024): 0xBC887D (0xBC8850, called by 0xC0D591 / 0xC38707 /
0xC69777) < 0xBC87D0 (0xBC8720) < vcall < 0xD673F8 (0xD673B0) < 0xACA42A (0xACA2A0) < 0xD576F1
(0xD57680) < 0xC27BA6 (0xC27B00) < vcall < 0xC1CEF0 (0xC1CA40) < 0xC0DB10.
The game's handler unwound past the galaxy-menu reset, which left the old player empire in the
menu: the next load crashed (BUG-0284, fixed there). This fault itself is not explained.
Seen once in ~100 logged leaves to the menu (out/lab logs, 2026-10-24..29).

Reproduce: not reproduced. The campaign's tribe had been played 30 min with 4 players and raids
(`coop.tribe pincer` shortcuts, an epic creature that "went away" 0.04 s after the quit).

## Root cause
Not known. A citizen's walk action reads a null object (+0x3C) while the stage is being torn down;
whether a mod object (a puppet, a pack member, the epic) is the target is not known.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-10-29: opened (fix/w8-planet, found while fixing BUG-0284).
