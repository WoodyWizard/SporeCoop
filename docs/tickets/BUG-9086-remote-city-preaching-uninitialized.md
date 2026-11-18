---
id: BUG-9086
title: Guest preaching stalls at cities the host has not viewed
status: fixed
kind: bug
stage: Civilization
reported: 2026-11-07
source: Native religious capture checkpoint
role: guest
versions: 0.7.3.6-lineages24-dev
fixed_in: 0.7.3.6-lineages25-dev
branch: feat/lineages-civ-combat
commits: c2c6d0d5
tests: tools/tests/lineages_civ_religious_controls.py
signature: Remote city preaching slots have zero population; preacher takes native retaliation without converting followers
docs: docs/re/civ.md
related: BUG-9084
---

## Evidence

On DuoQALineagesReligionN the guest sends actual religious city commands.
The host executes them and the native vehicle enters the city's preaching
slot. While the host remains at its home city, all rival slot+0/+4 values
are zero. No followers convert before the vehicle dies. Native damage probes
identify BEB6F8, within the city's own preaching routine, sending 12 damage,
type 1, rival faction, null attacker. Removed turrets are not the cause.

Moving only the host camera near the rival initializes all four slots to
10/2. A subsequent native single-preacher attempt converts followers and
captures the city on both peers. Evidence: lineages-religion-n3-controls.log,
lineages-religion-damage-probe2.log, lineages-religion-host-near.log and
lineages-religion-n5-controls.log. Both peers retain zero native faults.

The exact native initialization gate remains under investigation. Empty
nation species was also observed, but camera movement fixes the slot counts
without changing species, so that observation does not establish causation.

## Root cause and candidate fix

The camera-selected crowd controller initializes capacities through D57B00
and BD9FF0. Its two cCommunityLayout formations have counts computed by
AFDF60 as trunc(radius squared / spacing squared * 0.5). Native radii 7/3
and spacing 1.5 yield 10/2 independently of terrain. See
docs/re/civ.md#civ-remote-preaching for verified signatures and callers.

The civ.remote_preaching switch initializes only both-zero capacities in
an active native preaching slot on the host, using those native constants
and the native capacity setter. It preserves the visual controller, camera,
active counters, nonzero capacities, native conversion and retaliation.

FAIL: build/local/bug9086-fail-p.log, switch off. Native guest command reaches
the host; capacities remain zero and the preacher dies with followers 13/30.
PASS: build/local/bug9086-pass-p.log, switch on, same uninitialized city.
The single preacher converts and captures remotely. Both peers agree on
native ownership and city parts, with zero faults and no new crash report
after normal exit.

PASS: build/local/bug9086-group-q.log, independent profile Q. Its cold-city
single-preacher attempt initialized capacity but died before capture
(bug9086-pass-q.log, despite its provisional filename, is a failed run).
A four-preacher group then captured without any host-camera intervention.
Both games exited with zero faults and no new crash report after this run.
The test defaults to a small group; one preacher's survival is not guaranteed
by this initialization fix. No production health, power or retaliation was changed.

The private Lineages 25 package installs, uninstalls and reinstalls in a
disposable fixture while preserving loader, save and settings sentinels:
build/local/lineages25-installer.log. The packaged DLL also passes the related
Tribal combat regression. Full uninterrupted campaigns, four playable founders,
two-PC VPN sessions and arbitrary third-party mod combinations remain unqualified.

## History

- 2026-11-07: verified crowd-size computation and native setter, reproduced
  with the fix off, and completed remote native captures in profiles P and Q.

## Test correction

An initial city order may already target a native preaching-slot object by
the time it is inspected. Accept that verified native target/slot attachment
as well as the original city target. Merely counting forwarded commands is
insufficient. Spawning more vehicles at one point does not fix empty slots.
