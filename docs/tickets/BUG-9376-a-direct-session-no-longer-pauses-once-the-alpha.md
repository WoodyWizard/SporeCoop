---
id: BUG-9376
title: A direct session no longer pauses once the alpha is the live build
status: fixed
kind: bug
stage: All
reported: 2026-11-24
source: release check 2026-11-24
role: host
versions:
fixed_in: 0.7.14.1-lineages70-dev
branch: feat/lineages-alpha
commits: 389220d2
tests: tools/qa_win (manual, TITAN host + the MSI guest, a direct session): coop.pauses add 04BF38A8 on the host, coop.pauses on both
signature:
docs: docs/12-server-and-players.md#server-alpha-goes-live
related: BUG-9262, BUG-9227, BUG-9260
---

## Report
Release check for 0.7.14.1-lineages70-dev (owner 2026-11-24: "Fully push this to Lineages as Live."): the alpha's
rule 6T (BUG-9262: in a session P and the pause menu do not stop the world, no guest stands for the host) held in
every session with a second player, also a direct one (two friends, host and join by address, no galaxy server).
Live 69-dev paused such a session for everybody (BUG-9227 the cover, BUG-9260 the guest's game stands). With the
alpha as the live build every live player would have lost the pause.

## Root cause
Observed (code): coop.cpp no_pause_session() asked only for a connected session with another player; nothing in it
knew a galaxy planet from a direct session. 6T was decided for "the MMO / Alpha build", where a planet is shared
with strangers.

## Fix
coop.cpp no_pause_session(): false while the session does not run through a galaxy server (galaxy::relayed()), so a
direct session pauses as live did and a galaxy planet's session keeps 6T. Switch `coop.fix core.pause_direct off`:
no pausing in any session, as the alpha had it. Decision 8X (A applied, open for the owner).

## Verification
Direct session on the release build, TITAN host (profile DuoQAJiffHost, the Cell save Diplomavil) and the MSI guest
over Tailscale, 2026-11-24 about 18:28:
- FAIL (`coop.fix core.pause_direct off` on both): host `coop.pauses add 04BF38A8`; 4 s later the host and the guest
  both answer `pauses: flags=0 paused=0 reasons: none` (the pause was released at once, the guest ran on).
- PASS (the switch on, the default): host `pauses: flags=7 paused=1 reasons: 04BF38A8=1`, guest `pauses: flags=3
  paused=1 reasons: 04BF38A7=1` (the host-pause reason of BUG-9260) and the cover "Paused by host"
  (images/release70_direct_pause_guest_on.png); after `coop.pauses sub 04BF38A8` both answer `paused=0`.
- A galaxy planet's session is untouched by the change (relayed() is true there): not run again.
no-fail-run: Windows QA by hand (no lab on this PC); the FAIL is the same build with the switch off.

## History
- 2026-11-24: opened.
- 2026-11-24: fixed on feat/lineages-alpha (the release check of 70-dev).
- 2026-11-24: released in 0.7.14.1-lineages70-dev.
