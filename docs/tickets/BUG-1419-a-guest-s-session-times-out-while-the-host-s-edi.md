---
id: BUG-1419
title: A guest's session times out while the host's editor loads for more than 30 s on a slow PC (port of PR #216)
status: open
kind: bug
stage: All
reported: 2026-11-22
source: PR #216 (IltonBJSilva)
role: guest
versions: 0.7.16.1
fixed_in:
branch: fix/w63-editor
commits: 35163c73, 2a454d1e
tests: editor_stand_host, twin editor_stand_host_off; unit busy_long_wait (core_tests)
signature: "guest: connection to host lost (timeout)" while the host loads an editor (hang in App::cJob::Wait < cTextureManager < Palettes::PalettePageUI::Load)
docs: docs/04-netcode.md#netcode-editor-long-wait
related: BUG-1420, BUG-1142, BUG-0655, BUG-0687
---

## Report
PR #216 of WoodyWizard/SporeCoop by IltonBJSilva (their tickets BUG-9291 / BUG-9290, outside data, ported as our own
ticket; credit IltonBJSilva). Their integration lab on a loaded PC: at the Tribe -> Civ vote (City Hall editor) and
the Civ -> Space vote (spaceship editor) the host's game stood 45-52 s while the editor loaded. The guest heard
nothing for 30 s, its session ended ("connection to host lost (timeout)") and the host went on alone. Review:
out/research/prs-213-220.md (#216): the same code and the same bug on rel/0.7.16.

## Root cause
Inferred from the code (no run of ours): the guest ends its session after `Session::timeout_ms` (30 s) without a
packet from the host (net/session.cpp, the client timeout). The host's main thread stands inside the game's editor
load (not a message loop), so the keepalive (coop.cpp, BUG-0687) cannot help: it only queues run_outside_frame work
for that thread. The busy model (BUG-1142) told the guests "editor" only through its debounced Announcer once the
editor was open, after the stand.

## Fix
host_busy.cpp / busy_model.h / vote.cpp / net/session.h, switch `core.editor_long_wait` (on): right before an editor
request of the host's own reaches the game (vote.cpp submit_now for the held vote request, and the direct o_Submit at
the end of h_Submit: the PR covered only the held one), host_busy_long_wait() tells every guest MSG_HOST_BUSY Editor +
the new why bit kWhyLongWait (0x100) at once (Announcer::announce, no debounce) and flushes the session
(Session::flush_now). A guest sets its session timeout to busy_model::kLongWaitMs while the host's last told state is
such a stand (kWhyLongWait, Editor, or Away for a load), else the session's 30 s. kLongWaitMs is 120 s (the PR had
180 s; 120 s is over twice the 51.8 s seen and still notices a host that died in its editor within two minutes).
The host's timeout for its guests stays 30 s (the reverse case is BUG-1420). Wire: no change of a message, no family
bump: a new bit in the why field of the existing MSG_HOST_BUSY; busy_model::decode takes any why bits (an older guest
treats it as plain Editor), an older host never sends it (the guest keeps 30 s). Off: as 0.7.16.

## Verification
Open, waiting for the lab run (no lab runs during the 0.7.16 final check).
* Unit: tests/core_tests.cpp busy_long_wait (from the PR, 120 s): make test passes.
* Wanted PASS: `python3 tools/lab.py run editor_stand_host --lanes N` (the host's game stands 45 s after its editor
  request, `kit.test.hang 45000`: the guest stays in the session).
* Wanted FAIL: `python3 tools/lab.py run editor_stand_host_off --lanes N` (SPORE_FIX_OFF=core.editor_long_wait: the
  guest's session times out after 30 s).

## History
- 2026-11-22: opened (port of PR #216 by IltonBJSilva, 0.7.16.1 scope).
- 2026-11-22: fixed on fix/w63-editor (35163c73), tests 2a454d1e.

- 2026-11-22: renumbered from BUG-9304 at the 0.7.16.1 merge (the id came from the local fork branches pr-*); commits before the merge name BUG-9304.
