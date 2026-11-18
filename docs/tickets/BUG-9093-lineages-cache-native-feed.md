---
id: BUG-9093
title: Lineages Cache category labels and creation feed fail
status: fixed
kind: bug
stage: All
reported: 2026-11-07
source: Isolated native category prototype test
role: host guest
versions: 0.7.3.6-lineages26-dev
fixed_in: unreleased
branch: feat/lineages-cache
commits: 8bd7ca16
tests: tools/tests/test_cache_category.py tools/tests/lineages_library.py tests/library_sync_tests.cpp
signature:
docs: docs/re/creations.md#creations-cache-native-feed
related:
---

## Report and root cause

The native category rendered as `***`, mistaken for missing UI. The browser
requires a real locale reference. Fallback text with either zero or FFFFFFFF
references does not supply the caption. The original prototype also copied a
FeedItemMessage type from Rare Statues. FeedItemKeyList still displays zero:
its native enumeration queries a resource group and ignores the explicit keys.

## Fix and verification

Bundle UTF-8 BOM locale, group 02FABF01/type 02FAC0B6; reference real text IDs.
Use keylist_Template and a scoped native key enumeration adapter.
Negative evidence: out/cache-probe/dynamic-imported-feed.png.
PASS: out/cache-probe/feed-adapter-pass.png, final-native.log, final-restart.log
and release-native.log. Two native profiles receive four/seven creations,
retain native cards offline and avoid duplicate imports on reconnect. The
standalone shipping resources omit all vanilla root overrides; the DLL appends
our category to resolved properties, preserving existing categories.

The first native import was lost by the devkit's force-terminating quit,
which is TerminateProcess, not normal shutdown. Flush the native Pollination
database before reporting durable import success.
Cold restart resolves all eleven keys from native Pollination.package and
shows the cached creature/ship cards. All final native fault counters are zero.

## History

2026-11-07: traced live executable and reproduced both failures. Ticket created
manually because tools/tickets.py imports POSIX-only fcntl on Windows.
