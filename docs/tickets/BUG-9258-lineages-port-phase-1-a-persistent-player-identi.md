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
`make test` passes (SKIP_SANITIZERS=1, WSL Ubuntu 24.04). tools/tests/coop_identity.py is written and not run yet:
there is no Spore and no lab on the PC this was made on. On main the test fails (no coop.identity command); the
FAIL / PASS lab runs are still to do.

## History
- 2026-11-19: opened.
