---
id: BUG-0983
title: Tribe: the guest's game plays the conquest caption of a village the host conquered and stands paused until the guest clicks it
status: fixed
kind: bug
stage: tribe
reported: 2026-11-12
source: lab-final-0.7.13
role: guest
versions: 0.7.13
fixed_in: unreleased
branch: fix/w48-fmisc
commits: ea7552b3
tests: tribe_conquest_guest
signature: key frame 059: guest in TRG_TotemPole_Conquer caption, busy guest: cinematic,paused after 20 s
docs: docs/re/tribe.md#tribe-guest-village-caption
related: BUG-0531, BUG-0777
---

## Report
Key frame 059 (tribe_conquest_guest) of the 0.7.13 final check, lab 20261112-175447: 20 s after the host's raid
conquered a village, the guest's screen still shows the caption "You have destroyed the Cyan Village! ..." with the
game standing (`busy guest: cinematic,paused`), while the host's tribe went on (its caption closed after 8 s).
Player-visible: a guest who does not click the caption away has its game stopped; its tribe stands.

## Root cause
Observed: the realign conquers the village in the guest's game too (BUG-0777), and the guest's game then plays the
same village captions as the host's (TRG_TotemPole_Conquer 5418B038, TRG_Upgrade 94D17D9F), each waiting for its
player's OK with the input lock and the time standing. BUG-0531's 8 s close ran on the host only
(village_caption_tick in `if (is_host())`). docs/re/tribe.md#tribe-guest-village-caption.

## Fix
tribe.cpp village_caption_tick runs on guests too: a guest's village caption ends itself after 8 s
(`world::end_cinematic`, as the host's), no notice. Switch: `coop.fix tribe.conquer_caption_guest off` (waits for the
guest's click, 0.7.12). The test checks the guest's `busy` every 2 s after the conquest: free within 40 s.

## Verification
FAIL (SPORE_FIX_OFF=tribe.conquer_caption_guest): out/lab/20261112-212337-2 (tribe_conquest_guest: busy
cinematic,input_lock,paused for 41 s). PASS: out/lab/20261112-212603 (tribe_conquest_guest: free after 20 s, the
guest's log "ended it" for 5418B038 and 94D17D9F; tribe_away_conquest OK).

## History
- 2026-11-12: opened from key frame 059; fixed on fix/w48-fmisc (tribe.conquer_caption_guest).
