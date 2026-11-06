---
id: BUG-9036
title: Windows lab disables the audio events that advance Tribal performances
status: fixed
kind: test
stage: Tribe
reported: 2026-11-04
source: Guest instrument-input qualification
role: host
versions: duo10-dev
fixed_in: 0.7.3.6-duo10-dev
branch: duo-species
commits:
tests: tools/tests/duo_tribe_performance.py
signature: native session state4 round-count0 with -nosound
docs: docs/re/tribe.md
related: BUG-9035
---

The disposable Windows launcher used -nosound to keep test games quiet.
Native Tribal performance round length/count/requests arrive from AudioSystem
events. Disabling that system leaves the session at state4 with no rounds.
This is a lab configuration defect, not evidence that the guest species cannot
perform. The upstream Linux lab already mutes through profile preferences.

Control: m2-social-input-b.log uses same-frame native instrument posting but
still has zero rounds/progress with -nosound. m2-social-audio-a.log changes the
disposable launch to omit -nosound and sets AudioMasterVolume0 while retaining
AudioMuteAll false. The native session advances to state5, receives five rounds,
and reaches progress0.77 from ordinary instrument stimuli. Both pairs exit
with zero native faults. This probe alone does not qualify the final reward.

All subsequent Windows lab clones retain the audio engine and mute their own
profile volume. The performance regression test explicitly checks that the
native audio system exists before proceeding. No normal player profile or
system audio settings are changed.
