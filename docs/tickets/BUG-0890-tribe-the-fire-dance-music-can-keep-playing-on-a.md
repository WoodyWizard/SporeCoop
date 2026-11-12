---
id: BUG-0890
title: Tribe: the fire-dance music can keep playing on a guest when the relayed stop comes while the host or the guest is not in Tribe mode (editor, load)
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in: unreleased
branch: fix/w48-tribe-civ
commits: 9ce33b62
tests: tribe_party_editor
signature:
docs: docs/re/tribe.md#tribe-party-stop
related: BUG-0646, BUG-0568
---

## Report
Possible: the guest keeps hearing the fire-pit party music after the dance ended in the host's game; it plays on until the guest's next world change or session end. A guest that joins mid-party gets no music at all (the play is sent once; BUG-0646 says so).

Severity: low. Confidence: low.

## Root cause
Observed (code): the guest dropped every MSG_NPC_ANIM (kinds 7 / 8 too) while its own game was not in Tribe
(on_member_anims), so a stop that came while the guest was in an editor was lost and its local track
(g_party_local) played on; only party_forget at a world change or the session end stopped it. The host's
h_StopAudio sent the stop only while its own game was in Tribe (party_host()).
* Reproduced (lab 20261112-090930, `tribe.party_stop` off): the guest opened its own editor while both games played
  the party music; the host's party ended in Tribe 16 s later (the host's stop sent); back in Tribe 20 s after that
  the guest's game still played 0x19DC06DF (`coop.audio`: playing for 35 s).
* Observed (lab 20261112-085745): the host's game plays the party music on through its own editor (the party is not
  ended there); it stops when the host is back. So the host side's mode check never lost a stop in the lab; it is
  changed anyway (a stop of a relayed track goes out whatever the host's mode).

## Fix idea
Host: send the stop whenever the track was relayed, whatever the mode. Guest: handle kinds 7 / 8 in every mode, or stop the local track when the next host state shows no party (tribe +0x554 partying = 0). Resend the active play to a guest that joins or comes back.

## How to test
In tribe_party_music: start the party (`coop.tribe party`), put the guest into an editor mode, end the party on the host, bring the guest back; the guest's game must show no 0x19DC06DF track playing (`coop.audio`) within 3 s. Expected to FAIL today and PASS with the fix.

## Fix
tribe.cpp `tribe.party_stop`: a guest takes a party music stop (MSG_NPC_ANIM kind 8) in any mode (it stops its own
track for the host's); the host sends the stop of a track it relayed also when its own game is not in Tribe.
`party_stops_outside` counts both (`coop.tribe anims`, the party line `outside=`). Switch
`coop.fix tribe.party_stop off` (0.7.12).

## Verification
`coop_tribe_party_music.py --editor` (suite `tribe_party_editor`, tribe fixture, ~1.5 min): the party plays on both
games (BUG-0646 part), the guest opens its own editor (`coop.vote editor` on the guest), the host's party ends in Tribe,
the guest leaves its editor (the red X): its game must not play 0x19DC06DF (`coop.audio`, the game's own audio list).
* FAIL with `SPORE_FIX_OFF=tribe.party_stop`: lab 20261112-090930 (the guest played on 35 s after the host's stop).
* PASS: lab 20261112-091117, 20261112-091255.

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0646. Severity low, confidence low.
- 2026-11-12 (fix/w48-tribe-civ): reproduced (the guest in its editor at the stop), fixed (tribe.party_stop), FAIL and PASS runs in Verification.
