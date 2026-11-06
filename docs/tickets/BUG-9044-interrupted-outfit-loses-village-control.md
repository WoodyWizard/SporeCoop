---
id: BUG-9044
title: Leaving before an outfit is accepted can orphan the guest campaign choice
status: fixed
kind: bug
stage: Tribe
reported: 2026-11-05
source: Review of voted outfit persistence during disconnect
role: guest
versions: duo10-dev
fixed_in: unreleased
branch: duo-species
commits: 362148a
tests: tools/tests/duo_outfit_disconnect.py
switch: creations.outfit_ack
signature: guest choice persists before host outfit acceptance
docs: docs/re/tribe.md
related: BUG-9043
---

Code review: the guest saves its new local campaign choice and sends the new
model before the host applies and records that appearance. If the guest leaves
while the host is still in its editor, host pending state is discarded and
the saved founder history still names the old guest model. A reconnect can
then announce a new model that village_commander does not recognize.

Reproduce on disposable profiles: guest equips and saves a hat while host
stays in its own outfitter, guest leaves after local adoption, host exits,
guest rejoins. Assert native guest ownership and walking, not just the model
catalog or a pending flag. No runtime reproduction claimed yet.

## Investigation and correction

2026-11-05: control-a rejoined before the abandoned host transaction expired.
The host accepted the new model, and the test incorrectly indexed two different
snapshots by the old species. This was a test race, not a reproduction.
Control-b waits for token=0 before reconnecting. It reproduces three native
guest villagers with owner=254 while the client announces a new unrecognized
model. Both games report zero native faults. Evidence:
build/local/m2-outfit-leave-control-b.log.

The patch journals the previous local choice before adoption. Reliable outfit
kind2 acknowledges only the matching world/token/previous/result after the host
archives, records, and applies it. Disconnect or startup restores an outstanding
journal. A committed host with a lost acknowledgement still recognizes the old
appearance through founder history. This preserves control; it does not promise
that an unconfirmed cosmetic edit survives disconnection.

Fixed-a exposed an implementation error: WritePrivateProfileString(NULL,NULL,
NULL,path) returns zero even on a cache flush. The initial patch treated that as
failure. Corrected using Microsoft's documented return contract:
https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-writeprivateprofilestringa
The journal also pins the effective body before an editor changes the newest
catalog fallback, when the campaign record was unavailable at initial load.
Fixed-b used an older DLL because deployment began before compilation finished;
it failed at the UI vote prerequisite and is not qualification evidence. The
disposable deployment helper now refuses to run while compilation is active.

Fixed-c passes clean leave/rejoin, three native guest citizens with owner=1,
walking on both peers and zero native faults. Normal acceptance also passes:
m2-outfit-ack-normal-a.log, current models2915F4EA/7C0CBC65, guest local
choice2915F4EE retained and the rollback journal cleared.

Hard-exit-a/b did not qualify recovery: their restart helper omitted SteamAppId
and SteamGameId24720, causing native startup exit53 before coop loaded. The
helper now uses the established lab launch environment and waits for coop.status,
not merely the early SporeKit ping. Hard-exit-c reconnected with all3 guest
citizens owned by1, but the test still expected the original appearance. The
host had committed the result before detecting the crashed peer. CORRECTION:
recovery must permit either saved appearance while requiring the immutable
founder identity and native ownership. This case specifically exercises a lost
acceptance acknowledgement, not an abandoned host transaction.

PASS hard-exit-d: original founding identities retained, accepted appearance
read from saved lineage, all3 guest citizens controlled by1, native walking
on both peers, zero native faults. The original control-b failure, fixed-c
clean disconnect pass, normal-a acceptance pass and hard-exit-d recovery pass
qualify this fix. Local profile paths remain confined to disposable test data.

Follow-up review: a recovered guest may legitimately select an earlier key
after a lost acknowledgement. The next voted editor previously required its
previous key to equal the latest appearance, leaving this subsequent edit
pending indefinitely. Keep this ticket open through the resumed-editor test;
tribe.outfit_recovery permits a previous key only from the same saved founder's
history, still requiring the vote token and the sender's actual model bundle.

Resumed-editor control-a fails with the exact blocked tuple previous7CF26CCB,
current7C08BC0C, result7CCFED43 and pending1. Resumed-editor fixed-a passes new
models2915F883/7CEA50EB, all six native citizens, unchanged original founders,
walking on both peers and zero faults. This closes the recovered-editor path.

## Verification

FAIL build/local/m2-outfit-leave-control-b.log (native owner254 after rejoin).
PASS build/local/m2-outfit-leave-fixed-c.log (native owner1 and walking).
PASS build/local/m2-outfit-ack-normal-a.log (normal save, journal cleared).
PASS build/local/m2-outfit-hard-exit-d.log (process restart and walking).
FAIL build/local/m2-outfit-resume-control-a.log (next editor remains pending).
PASS build/local/m2-outfit-resume-fixed-a.log (next edit saved and playable).
