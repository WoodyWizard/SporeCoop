---
id: BUG-9345
title: Galaxy: the server's creation ban list reaches players when they connect (8D A)
status: fixed
kind: feature
stage: menu
reported: 2026-11-23
source: 
role: 
versions: 
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 5116ea44
tests: tools/qa_win (manual, 2026-11-23): TITAN, QA 9 on a copy of a test profile with 65 received creations, the rebuilt test galaxy; coop.fix galaxy.server_bans off/on; the server's player line with and without
signature:
docs: docs/12-server-and-players.md#server-ban-list
related: BUG-9329, BUG-9287, BUG-9290
---

## Report
Decision 8D A (owner 2026-11-23: "8DA"): the creation ban list (BUG-9329) shipped only inside the mod, so a new ban
reached players only with a release while the creation kept spreading. With the galaxy server, the server sends its
list too.

## Root cause
Not built yet (a feature).

## Fix
net/galaxy.h BANS / BANSPART. Server (server/galaxy/main.cpp): reads <worlds>/banned.txt (the format of the mod's
mods\lineages\banned.txt) at start and again when it changes (checked every 5 s); a player that sends BANS with a crc
other than the list's gets the list in pieces (again every 10 s until its BANS names the list's crc; an older mod
never sends BANS and gets nothing). `player <identity, 16 hex>` lines are the server's own: that player's creations
are not kept by the creation store (a put is answered as kept, nothing is written) and none it already has are handed
out (they stay on disk). `--no-bans`: no list. The status line counts the sends and the refused puts and gets.
Mod (library.cpp): on each connection it sends BANS with the crc of the list it keeps; a list that arrives is kept as
LineagesCache\galaxy_banned.txt (the last galaxy server's list), added to the shipped list's sets and applied at once
outside the frame (the same move out as at a game start: banned folder, moved.txt, keys hidden from the game's picks);
at each game start the kept copy is read beside the shipped list. A ban the server takes back stays applied until the
next start (a moved creation is never moved back, as with the shipped list). `coop.library status` shows
server_bans=<lists>/<moved>/<crc>. Switch `coop.fix galaxy.server_bans off`: nothing asked, kept or applied.

## Verification
TITAN, QA 9 on a copy of a test profile (65 received creations), the rebuilt test galaxy (copies), the server's list
banning one received creation (a `creation` line):
- FAIL (`coop.fix galaxy.server_bans off`, connect): server_bans=off, the server sent nothing (0 list sends), the
  creation stays in the cache's objects, no galaxy_banned.txt.
- PASS (on, still connected): server_bans=1/1/0579B1A2, ban_list 31 -> 32 creations, the PNG moved to the banned
  folder with its moved.txt line, galaxy_banned.txt holds the server's list.
- Next start without connecting: ban_list=36/32/80 (the kept copy read), server_bans=0/0/0579B1A2.
- The server's `player` line: without it, the player's creature (entering Samuth) was stored
  (07548D..._2B978C46_..._0667464E.crt); on a fresh galaxy copy with it, nothing of that player was stored and the
  server counted "1 puts ... of banned players refused". The same client lines (the player line is not sent) gave
  the same crc: no new download.
- Not run: a refused get (needs a second player asking for a banned player's creation), the MSI.
no-fail-run: a feature, Windows QA by hand; the runs above are the switch off and on.

## History
- 2026-11-23: opened.
- 2026-11-23: built on feat/lineages-alpha and seen in game (switch off: nothing moved; on: moved at once and at the next start).
- 2026-11-23: cross-PC run (the MSI): switch off nothing moved; on, the list arrived and the creation moved.
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
