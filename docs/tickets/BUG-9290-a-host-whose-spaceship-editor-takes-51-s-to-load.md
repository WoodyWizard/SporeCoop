---
id: BUG-9290
title: A host whose spaceship editor takes 51 s to load drops the guest (session timeout) at the Civ to Space vote
status: fixed
kind: bug
stage: Civ, Space
reported: 2026-11-20
source: agent test (lane C)
role: guest
versions: 
fixed_in:
branch: feat/lineages-space
commits: 8ecde240 (ticket only)
tests: agentC3/t8.py (by hand, lane C)
signature: host WATCHDOG (no frames) in IO::FileStream::Read < Resource::DatabasePackedFile::VerifyIndexRecordIntegrity < App::PropertyList::Read; guest "disconnected: connection to host lost (timeout)"
docs: docs/re/space.md#space-own-empires
related: BUG-9279, BUG-1142
---

## Report
Lane C, two games of the Steam exe on one Windows PC with three other lanes running. After the Civ team victory of the
Kealeneon fixture the vote "Player wants to edit a design (Civilization)" ended (everybody ready) and the host opened
the spaceship editor. The host's main thread hung 51.8 s inside the editor's first load (property files read from
the packages: the WATCHDOG stack above, hang report report-20261120-210503-i5). The guest saw no frames and no
message from the host for 30 s and left: "disconnected: connection to host lost (timeout)", its vote dropped. The
host went on alone (editor, launch, Space); the guest rejoined later straight into the host's Space world and so never
got the "open History and design your ship" step (its own ship and captain).

To reproduce: a slow disk or several games loading at once, then the Civ -> Space vote. The busy model (BUG-1142)
tells the guests "editor" only after the editor is open (21:05:24), too late.

## Root cause
Observed: a blocking load on the host's main thread longer than the session timeout. Inferred: the host should tell
the guests "loading the editor" before it asks the game for the editor (or the guests should wait longer while the
host's last busy state is a vote that just ended into an editor). 
## Fix
vote.cpp / session_glue.cpp (`core.editor_load_grace`): when a vote with an editor ends, a guest raises its session
timeout to 120 s for the next 180 s (`editor_grace_start`), then sets it back (vote_tick). Off: 30 s as before.


## Verification
no-fail-run: no Linux lab here. By hand on lane C (two games, 2026-11-20): the Kealeneon Civ fixture, the guest's
nation, every NPC city taken, History, the ship editor vote; when it ended the host ran `kit.test.hang 45000` (its main
thread blocked 45 s). `coop.fix core.editor_load_grace off` on the guest: "offline" after the block (FAIL); on: still
connected, the host in its editor (PASS).

## History
- 2026-11-20: opened.
