---
id: BUG-0873
title: Feature idea: a shared creations library (received creations in their own Sporepedia tab, kept between sessions)
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages42/43-dev, BUG-9093..9095, 9104..9106, 9112 (library.cpp, net/library_sync.cpp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Creations received from partners vanish after the session or fill My Creations. The fork keeps them in a
persistent library with its own Sporepedia tab ("Lineages Cache"), exchanged in the background.

## Root cause
Not a bug. RE (fork): Spore counts a creation as the player's own when cAssetMetadata author id (+0x68) is the
user's or -2 (offline author; Sporepedia 0x66DE73..0x66DE90); partners play offline too, so their creations look
like ours. My Creations is feed item 4E5892EB!146C1D8C (query C84E51B6 on root filter userCreated).

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages42/43-dev, BUG-9093..9095, 9104..9106, 9112; mods/coop/library.cpp, net/library_sync.cpp, MSG_LIBRARY 71, CAP_LIBRARY); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* Storage: PNGs under %APPDATA%/Spore/SporeCoop/LineagesCache with an index.bin; exchange paced in the background,
  host relay, reconnect dedup.
* Import through the native importer: App::Thumbnail_cImportExport::Get 0x5F79C0 and ImportPNG 0x5FC3C0
  (thiscall(importer, wchar_t* path, ResourceKey*), ret 8; false with the existing key for a known creation);
  inspect with DecodePNG 0x5FBB90 on an IO::FileStream (PathCtor 0x931DE0, Open 0x9318C0 args 1,3,1,0, AddRef
  0x931780: pin the stack stream before DecodePNG like native code, else the decoder deletes the stack, BUG-9094).
* Own tab: a category resource (locale group 02FABF01 / type 02FAC0B6) and a hook on FeedItem_GetAssetKeys 0x668D60
  that fills the tab and removes received keys from My Creations (BUG-9104).
* Failed imports retried 60 s later, three times per start (BUG-9106: a failure right after start was lost for good).
* Only the receiver decides whether creations a partner got from others are passed on ("Foreign Creations", BUG-9112).
* Large (about 1000 lines plus resources). Not in plan-0.7.12; would build on our creation stream.

## How to test
Lab, 2 instances: the guest makes 3 creations, the host gets them in the library tab and not in My Creations, they
survive a restart offline; a PNG made to fail its first decode is imported on the retry.

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
