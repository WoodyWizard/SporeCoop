---
id: BUG-0867
title: Feature idea: optional session password and a recent servers list in the co-op window
status: fixed
kind: feature
stage: All
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36-dev (net/session.cpp, coop_ui.cpp)
role: both
versions: 0.8.0
fixed_in: 0.8.0
branch: w08/lobby-qol
commits: fe6af56e 80e954c7 c8149c8b f61197e6
tests: password_recent, password_recent_off, net_tests::test_session_password, net_tests::test_recent_servers
signature:
docs: docs/08-ui.md#ui-lobby-qol
related: BUG-0874
---

## Report
Hosts on the internet want strangers kept out; players want to rejoin a friend's server without retyping the
address. The fork has an optional password and a list of joined servers (most recent first, LAN hosts marked).

## Root cause
Not a bug, a new feature.

## Fix idea
From the fork (PandizeLive/SporeCoop-Lineages lineages36-dev, mods/coop/net/session.cpp, protocol.h, coop_ui.cpp); NOT FINAL: taken from the fork, verify on our code and against our rules before using.
* HELLO capability bit CAP_JOIN_PROOF (128): the guest sends `sha256_hex(u32 connection id || password)` (64 hex
  chars, updater/sha256.h). The host compares with its own; on mismatch MSG_REJECT "Incorrect or missing server
  password." (with REJ_VERSION so older clients show the text). The proof is bound to the connection, so a
  captured HELLO cannot be replayed on another connection; it is not protection against offline guessing.
* The LAN discovery reply gets a settings byte: bit 0 paced transfer, bit 1 password required (and the scaling
  u16), so the server list shows a lock.
* Saved server addresses after a successful join, most recent first; the browser shows six at a time with wheel and
  a scrollbar; password and address fields can be masked (eye icons, Spore's native masking).
* Overlap: plan-0.7.12 F1 (co-op window 2.0: Join step, games found on the network, address field). Add the password
  field and the recent list to F1 rather than a separate window.
* Security: the password must not appear in logs or bug reports (our log masking, F4 report rules).

## How to test
Lab, 2 instances: host with password "x"; guest joins with none / wrong / right: refused twice with the text,
accepted the third time; a pre-feature guest is refused with a readable text; the LAN list shows the lock; after a
join the address is first in the recent list after a restart; the password is not in sporekit.log or the report zip.

## Verification
- Switches: `session.password`, `ui.recent_servers`
- Native unit tests & fuzz: `make unit-net unit-fuzz` PASS (`test_session_password`, `test_recent_servers`, `fuzz_decoders` `Hello` / `DiscoverReply`); `./build/host/net_tests --only test_session_password --off session_password` FAIL (6 assertions).
- Wine lab (lane 4, run `20261125-030621`, commit `f61197e6`):
  - `password_recent`: PASS (28 s, 0 faults on host & guest): host `password=yes`, LAN `[Locked]`, empty & wrong passwords rejected with `"Incorrect or missing server password."`, right password joins cleanly, recent server persisted in `coop.ini` `[recent]` across `mod.reload coop` and fills address on click, raw password never in `sporekit.log`, `coop.ini`, `coop.status`, or `coop.ui state`.
  - `password_recent_off` (`SPORE_FIX_OFF=session.password,ui.recent_servers`): FAIL (43 s, 8 assertions fail).

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
- 2026-11-25: implemented in WP-Q (`w08/lobby-qol`) with `CAP_JOIN_PROOF` (`16`), `DiscoverReply` `DF_PASSWORD` lock bit, `ITextEdit::SetTextEditFlag(0x80)` password masking + Show/Hide toggle, report redaction, and persisted `[recent]` servers list (`server0..server7`); verified ON PASS and OFF FAIL in lab run `20261125-030621`.
- 2026-11-25: released in 0.8.0.
