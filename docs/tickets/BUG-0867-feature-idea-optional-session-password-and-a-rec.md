---
id: BUG-0867
title: Feature idea: optional session password and a recent servers list in the co-op window
status: open
kind: feature
stage: all
reported: 2026-11-10
source: fork PandizeLive/SporeCoop-Lineages feat/lineages-43 lineages36-dev (net/session.cpp, coop_ui.cpp)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
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

## History
- 2026-11-10: opened from the fork review for the 0.7.13 plan (feature idea, fork evidence only).
