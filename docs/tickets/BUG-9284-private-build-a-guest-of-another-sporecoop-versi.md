---
id: BUG-9284
title: Private build: a guest of another SporeCoop version is refused at the join with a text on both games
status: open
kind: feature
stage: Menu
reported: 2026-11-20
source: owner 2026-11-20 (friends build test)
role: host, guest
versions: 0.7.15.94
fixed_in:
branch: build/amigos
commits:
tests: same_version
signature:
docs: docs/tickets/BUG-9284-private-build-a-guest-of-another-sporecoop-versi.md
related: BUG-1137, BUG-9265, BUG-9285, BUG-9286, BUG-9287, BUG-9288
---

## Report
The owner tested the private friends build 0.7.15.94 (separate lives: identity, own progress, own cards, own cells,
start choice, lineage cells) as host with a friend whose game ran the official 0.7.15.5. The join went through with
only a "versions differ" warning in the log and a short notice; in the Cell stage the friend's game did not do any of
the separate-lives parts (its progress followed the host's food total, it took the host's new body after the host's
editor, it could not use own cards). Nobody noticed that the two games ran different builds.

## Root cause
B6 (BUG-1137, owner decision 7) lets every other 0.7.x version in with a warning; only a message family of another
revision is refused. The friends build added minor revisions only (session 1.6, creations 1.1), so 0.7.15.5 fitted.

## Fix
`net::Session::same_version` (mods/coop/net/session.{h,cpp}, texts in net/compat.h): on, the host refuses a HELLO whose
coop version (the part before the first comma of the version line, SporeKit and the exe are not compared) differs from
its own, with REJ_VERSION and a text every 0.7.x guest shows ("not the same version: <host> has SporeCoop <v>, you
have SporeCoop <v>. This build plays only with the same version: install the same SporeCoop on both games"), and
notices on the host "<guest> (SporeCoop <v>) was not let in: this build plays only with the same version ...". A guest
with the gate on leaves the WELCOME of a host of another version (an older host lets everybody in) with the same
text. coop.ini `[coop] same_version` (default 1 on build/amigos only) and `coop.fix net.same_version off` (the
warning of 0.7.15). `coop.sameversion [as <v> | real]` shows the gate and, for the tests, makes a game claim another
version in its next HELLO / WELCOME. Unit tests of the comparison and the text in tests/validate_tests.cpp.

The setup and the in-game update (the friend had got the official 0.7.15.5 because SporeCoopSetup.exe run without its
.pack next to it downloads the newest official release from GitHub): on build/amigos the setup (SETUP_VERSION set,
inst::Options::private_version) installs only SporeCoop-<its version>.pack next to it (or /pack:) and never downloads;
without it (or with a pack of another version) it installs nothing and says "this is the private SporeCoop <v> setup:
put SporeCoop-<v>.pack next to the setup and run it again (it never downloads SporeCoop; nothing was installed)"; the
window asks GitHub nothing. autoupdate.cpp kPrivateBuild: no update check even with coop.ini [update] check=1, never
"available", Update does nothing.

## Verification
In the game, lane A on Windows (Steam build, instances 1/2, profiles dev1/dev2, both in the galaxy menu), driver
E:\Repositorio\SporeCoop-wt\agentA5\vgate.py (the steps of tools/tests/coop_same_version.py):
1. the same version joins;
2. a guest claiming 0.7.15.5 with its own gate off (as the official 0.7.15.5 has none): not let in, the host's
   counter 1, the host's notice "Bob (SporeCoop 0.7.15.5) was not let in: this build plays only with the same
   version...", the guest's notice "disconnected: rejected: ..." with both versions (shots vgate_host_text.png,
   vgate_guest_text.png in that folder);
3. a host claiming 0.7.15.5 with its gate off: this build's guest leaves at the WELCOME (counter 1);
4. twin: the gate off on both: the other version joins with the warning only.
The setup (silent mode, its log; nothing launched): the new SporeCoopSetup.exe alone in an empty folder with
/S /dir:E:\SporeLaneA: exit 1, "FAILED: this is the private SporeCoop 0.7.15.94 setup: put SporeCoop-0.7.15.94.pack
next to the setup ..." and the lane's coop.dll unchanged (same MD5); the same setup with SporeCoop-0.7.15.94.pack next
to it: exit 0, "OK: SporeCoop 0.7.15.94 is installed", mods/sporecoop.version 0.7.15.94.
no-fail-run: a new gate; without it (`coop.fix net.same_version off`, step 4) the other version joins. The Linux lab
run of `same_version` is left to the orchestrator.

## History
- 2026-11-20: opened.
- 2026-11-20: the gate on build/amigos, tested on lane A.
