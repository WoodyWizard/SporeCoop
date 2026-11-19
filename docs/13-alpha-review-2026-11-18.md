# Alpha verification, 2026-11-18 (feat/lineages-alpha)

Owner 2026-11-18: "I want you to verify everything we've worked on with this MMO branch of Lineages." What was done:
the build, every host unit test, the lints, three independent code reviews of the alpha's changes against live
(feat/lineages-47; about 7000 lines: the galaxy server and its protocol, parties and the Social UI, the player limit
and the world changes) and a live run on TITAN and the MSI. This file lists every finding and what became of it.
Status: **fixed** (in this pass), **open** (not fixed, why), **decision** (the owner's, docs/ROADMAP.md).

## Build, tests, lints
<a id="review-build"></a>

* The full build is clean. 21 host unit test suites pass (two needed a fix first, below).
* fixed: tests/net_tests.cpp did not compile on any compiler (`test_npc_compact` used before its declaration, since
  BUG-9235's commit); tests/validate_tests.cpp defined `g_nofix` twice (the upstream 0.7.14.1 merge). Both also on
  live. Nobody saw it: the unit tests are not run by the Windows build. net_tests then passes on the alpha (with the
  fan-out changes).
* fixed: validate_tests expected the old player limit (2..4); the alpha's is 2..64.
* fixed: tools/re_lint.py had 3 problems live does not have (BUG-9255: the frozen GOG 2016 build has no symbols for
  the galaxy menu's planets and era visitors; the lines saying so were missing).
* open: tools/tickets.py check lists 16 older tickets with missing fields (same as live).

## The galaxy server and its protocol (server/galaxy/main.cpp, net/galaxy.h, galaxy_client.cpp)
<a id="review-server"></a>

No out-of-bounds read or write was found; slots cannot be spoofed in relay or fan-out; the wire formats match on both
sides. The findings are about a server that will face strangers:

| # | Finding | Status |
|---|---------|--------|
| S1 | Any connected player could make the server allocate 128 MB per datagram (XBEGIN allocated before the sender was checked) and hold 128 MB per slot | fixed: checked first; a world is 32 MB at most (`--max-world-mb`); a thrown allocation no longer ends the server |
| S2 | The identity in HELLO is self-declared and sent to everyone: a client that says another player's identity takes that player's party seat and removes them from it | decision 6S (a secret per identity, kept by the server on first use) |
| S3 | No proof that a HELLO's address is real; amplifiers (fan-out listing one slot 64 times, a download nobody acks resent forever, LIST, REFUSED) | fixed: each slot once per fan-out; a download without acks is given up after 30 s. decision 6S: a cookie in HELLO |
| S4 | FETCH unthrottled; each one read the world and ran its CRC twice, and the CRC again twice a second until the first ack | fixed: one FETCH per 3 s and per download; the CRC once |
| S5 | READY and LEAVE go once over UDP: a lost one left the planet "being loaded" or a player on a planet it left (it could even be made host) | fixed: the pings carry the game's planet state; the server follows it after 3 pings |
| S6 | An empty planet belongs to whoever enters first: they can replace its stored world or lock it by never getting ready | decision 6S (who may host and store a planet) |
| S7 | No limits on players, worlds, disk; party ghosts and upload facts never purged | fixed: `--max-players` 1024, `--max-per-address` 64, `--max-worlds` 500, planet names 64, player names 32; ghosts purged. open: a log line per HELLO |
| S8 | Entering another planet while hosting dropped the hosted planet's last save, and the stale "leave after the save" threw the player off the new planet later | fixed: the hosted planet is saved and left first ("enter again in a moment") |
| S9 | A guest's ROLE handled inside Session::update joined from there: read as a join timeout at once | fixed: joined from the tick |
| S10 | The planet list stops at 40 and the player list at one datagram (about 33 players): no paging | open (fine for the alpha's size) |
| S11 | A stored world with a 243+ byte planet name did not load at a restart; two crafted names can share a file | fixed (the header buffer; names 64 at most). open: the crafted collision |
| S12 | Party: "X declined" notes for answers to no invite; lapsed invites still shown as pending; a new invite replaces a pending one | fixed (the first two). open: the last |
| S13 | After "The galaxy server stopped answering" a game stayed unwelcomed although the server answered again | fixed (HELLO again on a PONG) |
| S14 | Player and planet names went raw to other games and the console | fixed: one line, no control characters |

## Parties, the Social Menu, whispers (parties.*, coop_ui.cpp, coop.cpp)
<a id="review-parties"></a>

The book's rules hold (4 members, one party each, the longest standing member leads, kick and lead only by the
leader); fixed arrays are bounded; the session's sender ids are trusted correctly.

| # | Finding | Status |
|---|---------|--------|
| P1 | Galaxy: a lapsed invite came back as a prompt that could not be answered (the server never lapsed invites) | fixed: the server lapses invites every second; an answer to a lapsed one removes it |
| P2 | A direct session played while still connected to a galaxy server used the galaxy's party rules (no Party cards, invites "not here", migrations alone) | fixed: the galaxy keeps the parties only without a session or in a planet session |
| P3 | The host dropped another party's migration warning (in its editor, or within its own round) before passing it on | fixed: on a galaxy planet the host passes a guest's warning on before its own checks |
| P5 | An invite prompt stayed live but invisible when the Co-op window closed: clicks there were swallowed, one could accept the invite | fixed: the menu is laid out again when the Co-op window opens or closes (not seen at runtime yet) |
| P6 | Names from the galaxy server were not cleaned on the game's side (a line break forged feed lines and friends.txt entries) | fixed: cleaned on the server (S14) and again in the game |
| P7 | The invite prompt was lost when another player menu opened | fixed: the prompt comes back when the other menu is closed (not seen at runtime yet) |
| P8 | Whisper buttons did nothing where the chat line is not on screen (the galaxy menu, the Settings page) | open: Whisper now says where whispers are typed when there is no chat line; a chat line in the galaxy menu is not built |
| P9 | "Invite to Party" on an offline friend without a galaxy did nothing, silently | fixed: "That player is not reachable from here." |
| P10 | Menu whispers went by name: the first player of that name got them | fixed: a menu whisper goes to the player picked (for 5 minutes, while the line still names it) |
| P11 | An invite outlived its sender leaving or being kicked from the party it was sent from | fixed: a player's invites go when it leaves or is removed (party_tests) |
| P12 | Session host: an answer's note went to a guest-chosen player before any invite was checked; "left their party" announced for a player already alone | fixed |

## The player limit, Cell, worlds (cellsync, founders, tribe, civ, world, creations, era visitors, galaxy menu)
<a id="review-scaling"></a>

Per-player tables are maps or sized by the new ceiling; the old 4-player masks are gone; the compact Cell reader checks
every field; a stored-copy join that does not match ends in the safe direction (the host sends its world).

| # | Finding | Status |
|---|---------|--------|
| W1 | Compact Cell state: a "keep" record did not refresh the mirror's last-seen time, so one lost chunk deleted far cells for up to 2 s | fixed: a keep counts as seen (not measured on a lossy link yet) |
| W2 | The stored world's FETCH went once: lost, the player sat on "Downloading..." forever; a REFUSED made a joiner leave instead of joining the old way; a world of 0 bytes hung | fixed: FETCH again every 4 s; no progress for 30 s, a refusal or a damaged copy: a guest joins its host the old way, a host-to-be leaves with a message |
| W3 | The host's recorded own creature (5U A) may put the pre-evolution body back for up to 10 s after the host evolves (code reading; not seen at runtime) | fixed: the host's record follows the creature it just made and the creations are read again at once (not seen at runtime) |
| W4 | MSG_TRIBE_STATE cannot carry more than about 21 villages: at 25+ players on a Tribe planet guests get no Tribe state | fixed 2026-11-19 (BUG-9271: the table on its own, MSG_TRIBE_VILLAGES; 32 villages stay the limit). Before: guarded (an error line instead of a wrapped size; no Tribe state above about 21 villages). The village table needs its own message before a Tribe planet takes more than 16 players |
| W5 | galaxy_install sized a vector from the blob before checking it and skipped the "in an editor: later" guard; the missing-parts check cannot run before a stored load | fixed: the count is checked first; no load under an open editor. open: the missing-parts check before a stored load |
| W6 | A hand-over from the main menu can pack the save folder's first planet instead of the galaxy planet | fixed: the galaxy client names the planet's save file it played |
| W7 | founders::model_ids: the "never refuse on their number" break still failed the load (over 1088 archives only) | fixed |
| W8 | 8 player colours for up to 64 players: shared colours from the 9th, and nobody can change colour then | fixed (the refusal): colours are shared once all 8 are in use. open: more colours |
| W9 | Era visitors (dev spike): props forgotten, not destroyed, when the mode leaves Creature | open (a spike: off by default) |
| W10 | galaxy_menu: a half-failed hook setup left a hook calling a null original | fixed |

## The live run (TITAN QA 8 and 9, the MSI; the fixed builds and the fixed server)
<a id="review-live"></a>

* tools/tests/galaxy_server_check.py (new, no game): 15 checks of the server's rules pass; 9 of them fail on the
  server built before the fixes (GALAXY_CHECK_OLD=1).
* ✔ The MSI hosted Samuth from the galaxy's stored world (4006 chunks, READY). ✔ QA 9 and QA 8 joined; QA 8 through
  the stored copy (the host: "stored joins: accepted 1", no world sent), joined from the tick (S9's new path).
* ✔ Copies: the server's "copies out" ran at 2-3.5 KB/s for 3 players. ✔ A party of QA 8 and QA 9: invite, yes, kick,
  invite again, the lead handed on.
* ✔ The hand-over: the MSI left ("Saving Samuth to the galaxy before leaving"), the world was stored, QA 9 hosted the
  world it played 3 s later, QA 8 joined it, the party was still there.
* Earlier the same day (before this pass): the Social Menu, the native frame, the migration rule (fix on, off, a
  party), centred name plates.
* NOT run (the owner ended the session to play live): the server restart with players connected, "enter another planet
  while hosting", a Tribe and a Cell planet with the fixed builds, the invite prompt and the menu over the Co-op
  window, a whisper's arrival (the session whisper was sent; its arrival was not read back).
* Run 2026-11-19 (TITAN QA 8 and 9): ✔ a whisper arrives (the guest's chat: "Pandize whispers: ..."); ✔ the invite
  prompt shows in the world, stays over the open Co-op window and after it closes (P5, P7); ✔ entering another
  planet while hosting saves and hands the first one over ("enter Clyth again in a moment", S8); ✔ Tribe (Clyth,
  BUG-9271) and Cell (Cellfour, BUG-9266) planets with the fixed builds. Found and fixed: the server restart left
  every game on no planet, and a 24 s freeze while leaving a stage did the same (BUG-9272); a host's own reload of
  its planet handed it over (BUG-9268).

## What is left
<a id="review-left"></a>

* decision 6S (docs/ROADMAP.md): before strangers can reach a galaxy server: a proof of identity (S2), a proof of
  address (S3) and who may host and store a planet (S6).
* open, no decision needed: W4 (the Tribe village table), P8 (a chat line in the galaxy menu), W5's missing-parts
  check, S10 (paging), W8 (more colours), W9 (the era visitors spike).
* 6R A's second half, per-party difficulty (the 6K plan's step 1): Creature built 2026-11-19 (BUG-9266,
  docs/12-server-and-players.md#server-party-difficulty); Cell, Tribe and Civ and relations per party built the same day
  (6V A and B, docs/12-server-and-players.md#server-party-relations).
