---
id: BUG-9075
title: Incoming capture mutates the city held by the guest planner
status: fixed
kind: desync
stage: Civilization
reported: 2026-11-06
source: Duo 18 open-planner capture qualification
role: guest
versions: duo18-dev
fixed_in: unreleased
branch: feat/lineages-civ-combat
commits: 39cb1737
tests: tools/tests/duo_civ_capture_planner.py
signature: Open planner city changes from 2231 to 2230 with mixed building owners
docs: docs/re/civ.md
related: BUG-9074 BUG-9073
---

## Report

During native planning in the guest's capital, the host captured that city
for a rival. Before the guest closed the planner its city owner changed
from 2231 to 2230 and the building list grew from one 2231 building to
two buildings with mixed owners. Both games reported zero native faults,
but the native editor still held objects belonging to the previous owner.
The failing run is build/local/lineages-capture-planner-before.log.

## Root cause

The ownership repair already waited for the planner. The earlier city
snapshot still invoked ChangeOwner immediately, and building snapshots
and incremental construction could mutate the same city's objects.

## Fix

Keep receiving host ownership metadata so commands still enforce the real
owner. Defer native city transfer and building reconciliation while that
city's planner is live. The next regular snapshots apply the current host
state after the editor closes.

## Verification

FAIL on the Duo 18 packaged DLL in lineages-capture-planner-before.log.
PASS in lineages-planner-hold-a.log and lineages-planner-hold-b.log.
Both passes retained faction 2231 and its existing building while editing,
then converged to rival faction 2232 after closing the planner.
The second pair also passed normal turret purchase, movement, sale and
cancelled placement in lineages-wall-hold-b.log. All games reported zero
native faults and exited normally.
