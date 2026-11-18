---
id: BUG-9104
title: Lineages Cache: received creations also show in My Creations
status: fixed
kind: feature
stage: All
reported: 2026-11-09
source: player request (roadmap 42, cache decision 3B)
role: all
versions: 0.7.3.6-lineages41-dev
fixed_in: 0.7.3.6-lineages42-dev
branch: feat/lineages-42
commits: 63abe7bf
tests: QA Sporepedia counts (single PC), see Verification
signature: -
docs: docs/re/creations.md#creations-cache-my-creations-split
related: BUG-9093
---

## Request

Creations received from partners belong in the Lineages Cache tab only, not in My Creations,
provided worlds can still use them (decision 3B).

## Root cause

Spore treats a creation as the player's own when its metadata author ID (cAssetMetadata +0x68)
is the signed-in user's or -2, the offline author every creation made without an EA account
carries (Sporepedia 66DE73..66DE90). Partners play offline too, so their creations carry -2 and
count as the player's own. My Creations is the feed item 4E5892EB!146C1D8C: query type C84E51B6
on the root filter `userCreated` (fnv 37216F9C; filter prop CC489C6F!37216F9C lists only
sub-filters, no ownership rule).

## Fix

`h_FeedKeys` (the FeedItem_GetAssetKeys hook the cache already has) lets the game fill the My
Creations list, then removes the keys of received cache creations (index entries not local, not
failed). Nothing is saved or changed in the creations: they stay in Everything, in the Lineages
Cache feed and in the game's pollination data. `coop.fix library.split off` restores the old
list. `coop.library` shows `mine_hidden`.

Tried first and dropped: answering author -3 from cAssetMetadata::GetAuthorID (0x5508A0) for
received keys, also during the import (the Sporepedia index stores an author hash, parameter
0518AF1D, when an asset is imported, 56C5D9). The import was indexed with -3 and still listed in
My Creations, so the list's query does not go by that hash.

## Verification

no-fail-run: new feature (nothing to fail before it); checked by hand on the Windows QA copy below, no lab test yet

Single PC, QA copy, profile LineagesDepthQA, 800x600, muted: two received creations imported
with `coop.library import`. Before: My Creations (8), Everything 2598 = Maxis 2590 + 8. After:
My Creations (6) and 6 cards, Lineages Cache > Shared Creations (2) with both, Everything 2598
unchanged; mine_hidden counts the removals; 0 faults.

Not verified: that the game's world population still picks received creations (decision 3B's
condition). The change is in the Sporepedia list only; pollination reads its own database.

## History
- 2026-11-10: built on feat/lineages-42; single-PC QA above.
- 2026-11-10: released in 0.7.3.6-lineages42-dev.
