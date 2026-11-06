---
id: BUG-0568
title: Tribe: no music in the guest's Tribe game
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-04
source: player report 2026-11-04 (Discord bug-reports, 0.7.5, Steam build)
role: guest
versions: 0.7.5
fixed_in: 0.7.8
branch: fix/w44-tribe-guest
commits: 6236a107
tests: tribe_theme_music (tools/tests/coop_tribe_theme_music.py)
signature:
docs: docs/re/tribe.md#tribe-guest-a1
related:
---

## Report
The guest: it has no music in the tribe stage.
Reports: SporeCoop-report-2026-11-04 191227 (out/reports/2026-11-04, not in git). Not analysed yet.

## Root cause
Logs: the reports' sporekit.log of both players (host 17:26-20:15, guest 17:27-19:45 and 19:47-20:09, lined up by the session clock `[s N]`).
* No log line about music. Inferred: the game's Tribe music follows the player tribe's own state (fights, the
  village's mood); the guest's tribe holds only mirrors. Not analysed.

2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8): the lab games are muted, so the game's audio commands were recorded
(`coop.audio`, mods/coop/audio.cpp: every command Audio::AudioSystem submits, sound id, track, caller).
* **Observed** (lab lane 9, tribe fixture): the Tribe stage's own sounds after the load (the mode's sound group of 10
  sounds 0x34D61238 ..., 0xDDF535CD x3, 0x4DCE76BE x3, 0x67A81768 x16) were the same on the host and the guest, also
  after a guest's own editor visit (out/lab/20261105-110707, 20261105-111654). In a 40 s fight of the host's members
  the host's game played the stage's **hunt / fight theme music** 0xAD1A17D 7-11 times (procedural, through the stage's
  theme music manager TribeStage +0x1C8, SporeCoop::TribeTheme_Play 0xBC23B0, from the behaviour 0xD69740), the guest's
  game none (out/lab/20261105-115203, 20261105-115653). The themes are started by the player tribe's members'
  behaviour trees, and a guest's members are mirrors without one (tribe.cpp h_BTTick).
* Inferred, not reproduced: the report's guest came into Tribe by the live Creature -> Tribe change, a quit to the
  galaxy menu 19 s later and the load of the host's world (and an editor visit 40 s after): that path was not replayed
  (the lab loads the fixture from the menu); its stage sound group may differ too. The galaxy menu's music
  (0xBD7F5BC9, and 0x54AB9C8A / 0x896ED613 after the menu intro) is no Tribe music (a lab artefact on the host: the intro
  ended in the stage). The fight's hit sounds (0xA7E3E1FB, 0xD05B58ED, ...) are also host-only: BUG-0600.

## Fix
`tribe.theme_music` (tribe.cpp): the host hooks the theme manager's play / move / stop (SporeCoop::TribeTheme_Play,
_Move, _Stop) and relays them to the guests (MSG_NPC_ANIM kinds 4 / 6 reliable, kind 5 moves at most every 0.3 s a slot);
a guest plays them on its own stage's theme manager (its own volume and music settings), its slots mapped to the
host's, forgotten at a world change. Older guests ignore the kinds. `coop.fix tribe.theme_music off`: nothing sent or
played (0.7.7).

## Verification
tribe_theme_music: PASS out/lab/20261105-115510 (host 8 theme plays, guest 8, sound 0xAD1A17D); FAIL with
SPORE_FIX_OFF=tribe.theme_music out/lab/20261105-115653 (host 11, guest 0). Threshold: guest >= 80 % of the host's.
Not reproduced (the lab games run muted). Not in the plan table: P1 (the guest as a player) suggested.

## History
- 2026-11-04: opened.
- 2026-11-04 (fix/w43-replay, step 0 of 0.7.7): replayed on 0.7.6 code; still open, not analysed (lab games are muted).
- 2026-11-04 (fix/w43-guest-player, P1 of 0.7.7): not analysed in P1 (lab games are muted).
- 2026-11-05 (fix/w44-tribe-guest, A1 of 0.7.8): analysed with the game's audio commands; the fight theme observed missing on the guest, fixed by tribe.theme_music; the report's exact path (live change + menu bounce) not replayed.
- 2026-11-05: released in 0.7.8.
