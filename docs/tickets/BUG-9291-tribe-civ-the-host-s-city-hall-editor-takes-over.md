---
id: BUG-9291
title: "Tribe -> Civ: the host's City Hall editor takes over 30 s to open on a loaded PC and every guest's session times out"
status: fixed
kind: bug
stage: Tribe, Civ
reported: 2026-11-20
source: integration campaign QA lane B (test/integration)
role: host, guest
versions: 0.7.15.94
fixed_in: unreleased
branch: fix/editor-load-timeout
commits: 50ee8b9e
tests: unit (tests/core_tests.cpp busy_long_wait), agentB in-game runs below
signature: "hang: App::cJob::Wait < cTextureManager < Palettes::PalettePageUI::Load; guest: connection to host lost (timeout)"
docs: docs/04-netcode.md#netcode-editor-long-wait
related: BUG-9290, BUG-0655, BUG-0576, BUG-1142
switch: core.editor_long_wait
---

## Report
Integration campaign QA (lane B, test/integration, planet Epina, 2026-11-20 22:06): the Tribe -> Civ vote ended; the
host's game then drew no frame for more than 30 s while it opened the City Hall editor (SporeKit hang report: main
thread in App::cJob::Wait under cTextureManager, from Palettes::PalettePageUI::Load, the editor's palette images). The
guest's session ended at the same moment ("connection to host lost (timeout)", last heard 29.6 s before); the host went
on to Civ alone; the guest stayed in its Tribe world until it joined again. The Space agent's BUG-9290 is the same class
(the spaceship editor's first load, 51.8 s).

## Root cause
Observed: the host's main thread waits for the game's own load; the host sends nothing meanwhile, and a guest ends its
session after 30 s without a packet (net/session.cpp). The busy model (BUG-1142) told the guests "editor" only once the
editor was open, after the stall, and the guest's timeout did not depend on it. The host's side keeps its guests: the
packets they sent wait in the socket and are read before its timeout check.

## Fix
host_busy.cpp / busy_model.h / vote.cpp (`core.editor_long_wait`): just before the host asks the game for an editor
(vote.cpp's outside-frame submit), host_busy_long_wait tells every guest Editor + kWhyLongWait at once (no debounce) and
flushes the session (Session::flush_now). A guest sets its session timeout to busy_model::kLongWaitMs (180 s) while the
host's last told state is such a stand (kWhyLongWait, Editor, or Away for a load), else the session's 30 s. Older guests
ignore the new bit; an older host never sends it (the guest keeps 30 s). Off: as 0.7.15.5.

## Verification
Lane B, the integration build, `kit.test.hang 45000` on the host right after a vote into an editor ended (a forced
45 s stall of the host's main thread while the editor loads):
- PASS (planet Orley, Tribe -> Civ, the City Hall vote): the guest saw `host=editor why=0100 ... timeout=180000 ms`
  during the stall and was still connected after it; the host in its editor.
- FAIL, twin on the host (planet Orley, Creature -> Tribe vote, `coop.fix core.editor_long_wait off` on the host): the
  guest heard nothing before the stall (`host=none ... timeout=30000 ms`) and was offline after it.
- FAIL, twin on the guest (the same switch off on the guest only, a 45 s stall while the host sat in its editor): the
  guest went offline.
- Unit: tests/core_tests.cpp busy_long_wait (the announce at once, the guest's timeout per told state). make test passes.
- no-fail-run: the Linux lab is not available here; FAIL and PASS above were run by hand on lane B
  (agentB/b_9291t.py, b_twin.py).

## History
- 2026-11-20: opened (fix/campaign-qa).
- 2026-11-20: fixed on fix/editor-load-timeout (from 0.7.15.5); the open copy left fix/campaign-qa.
