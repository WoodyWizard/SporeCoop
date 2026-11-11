---
id: BUG-0890
title: Tribe: the fire-dance music can keep playing on a guest when the relayed stop comes while the host or the guest is not in Tribe mode (editor, load)
status: open
kind: bug
stage: Tribe
reported: 2026-11-10
source: fixed-ticket review 2026-11-10 (0.7.13 plan)
role: guest
versions: 0.7.11.1
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0646, BUG-0568
---

## Report
Possible: the guest keeps hearing the fire-pit party music after the dance ended in the host's game; it plays on until the guest's next world change or session end. A guest that joins mid-party gets no music at all (the play is sent once; BUG-0646 says so).

Severity: low. Confidence: low.

## Root cause
* Observed (code): mods/coop/tribe.cpp:1510-1515, h_StopAudio clears g_party_host_track and sends the stop (kind 8) only when party_host() is true; party_host() (tribe.cpp:1482) needs `mode_id() == kTribe`, so a stop that runs while the host is in an editor or loading is lost.
* Observed (code): tribe.cpp:1581, the guest drops every MSG_NPC_ANIM (kinds 7 / 8 too) while its own mode is not Tribe, so a stop that arrives while the guest is in an editor is lost and its local track g_party_local plays on; only party_forget from on_world_change / on_session_end (tribe.cpp:4026, 4047) stops it.
* Inferred (low): whether the party behaviour's exit (StopAudio 0xD78F80) runs while the host is in an editor is not known.

## Fix idea
Host: send the stop whenever the track was relayed, whatever the mode. Guest: handle kinds 7 / 8 in every mode, or stop the local track when the next host state shows no party (tribe +0x554 partying = 0). Resend the active play to a guest that joins or comes back.

## How to test
In tribe_party_music: start the party (`coop.tribe party`), put the guest into an editor mode, end the party on the host, bring the guest back; the guest's game must show no 0x19DC06DF track playing (`coop.audio`) within 3 s. Expected to FAIL today and PASS with the fix.

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-10: opened by the fixed-ticket review (plan 0.7.13), from BUG-0646. Severity low, confidence low.
