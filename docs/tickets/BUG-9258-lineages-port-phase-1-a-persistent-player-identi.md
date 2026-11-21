---
id: BUG-9258
title: Lineages port, phase 1: a persistent player identity per Spore profile (MSG_IDENTITY)
status: open
kind: feature
stage: All
reported: 2026-11-19
source: PR #196 (Lineages, PandizeLive): port onto 0.7.15.2
role: both
versions: 0.7.15.2
fixed_in:
branch: feat/lineages-port (fork IltonBJSilva/SporeCoop)
commits:
tests: tools/tests/coop_identity.py (suite identity), tests/validate_tests.cpp (MSG_IDENTITY row)
signature:
docs: docs/04-netcode.md#netcode-identity
related: BUG-1149 BUG-0794
---

## Report
The owner wants Separated Lives of Lineages (PR #196): every player with their own species, nest, village, nation and
ship, keeping the consequences of their own choices. PR #196 is based on 0.7.14.1 and conflicts in 44 files (about 230
hunks) with 0.7.15.2, mostly because 0.7.15 split coop.cpp, tribe.cpp, civ.cpp and cellsync.cpp while Lineages grew
them. The port goes in phases; phase 1 is the layer the rest builds on: a player identity that survives rejoins,
reloads and new network ids (Lineages BUG-9135 / BUG-9198).

## Root cause
Network player ids are reused (the lowest free one) and names can change, so with more than one partner neither tells
the host whose founder village or Space ally list is whose after a rejoin (Lineages BUG-9135 / BUG-9198, from their
code and tickets; not seen on our side).

## Fix
identity.h / identity.cpp from Lineages, wired into 0.7.15's structure: MSG_IDENTITY 73 in net/messages.def (route
Identity, session family minor 1 -> 2, lock written), session_glue.cpp dispatch and player leave, a lifecycle hook for
the session end, identity::init / tick in coop.cpp, the command coop.identity, net::Writer / Reader u64,
creations::coop_dir. Switch: `coop.fix identity.keys off` (key() falls back to the name).

## Verification
`make test` passes (SKIP_SANITIZERS=1, WSL Ubuntu 24.04).
Run in the game on 2026-11-19, Windows 11, Steam build (SporebinEP1\SporeApp.exe 25 066 744 bytes, `3010a413...`),
two instances on one PC (`-sporekit-profile:dev1`, `-sporekit-instance:2 -sporekit-profile:dev2`), the steps of
tools/tests/coop_identity.py by hand over the debug console (the lab runner is Linux only):
* after the join: the host `known=1 received=1 p1=07BDCA5A`, the guest `tables=0/1 p0=1E9EAA7B p1=07BDCA5A`: each game
  knows the other's identity, the two profiles have different ones
* the guest leaves: the host `known=0`; it joins again: the guest's own is the same (`07BDCA5A8844CF37`), the host
  `received=2 p1=07BDCA5A`
* `coop.fix identity.keys off` / `on` answer; no faults on either game
no-fail-run: a new feature, nothing to fail before it (on main `coop.identity` is an unknown command); the lab runs of
tools/tests/coop_identity.py are still to do on the Linux lab.

## History
- 2026-11-19: opened.
- 2026-11-19: phase 1 ported (feat/lineages-port), tested in the game on two instances (Windows, Steam).
