---
id: BUG-9012
title: A guest drives a freed Creature proxy between its one-second liveness checks
status: fixed
kind: crash
stage: Creature
reported: 2026-11-03
source: Duo recovery test with a long native death caption
role: guest
versions: 0.7.3.6-duo9 development
fixed_in: 0.7.3.6-duo9
branch: duo-species
commits: fb08eac
tests: tools/tests/duo_creature_rescue.py --hold-death 125
signature: coop D1A80 null vtable read 44 via npc guest_tick and drive step
docs: docs/re/creature.md#creature-proxy-before-drive
related: BUG-0300, BUG-0426, BUG-9011
---

## Report

After the host completed its recovery on fresh Vantiegan, the guest was revived
then killed. Its native death caption was held for 125 seconds and confirmed.
The guest crashed before hatching. The running scene was F0428C29, not the
FBB3661D migration watched by BUG-9011. No migration recovery was invoked.

## Evidence

build/local/m1-migration-fix-guest.log fails with a closed connection.
build/local/m1-death-caption-crash.txt preserves crash-20261103-230048-i5.txt.
The matching DLL resolves D1A80 to the teleport vcall at offset 44, DCE95 to
drive::step, EC56B to npc::guest_tick line 1161 and E825B to npc::tick.
The object has a null vtable. The native guest log is preserved alongside it.

## Root cause

Observed: the driver called a destroyed object's virtual function. Inspection:
BUG-0300's native noun-list membership check runs only once per second. Between
checks the driver trusts mbIsDestroyed in a cached pointer. Freed storage can
have that byte clear again. The one-second gap is the suspected entry path;
the fix checks live noun-list membership before every drive pass.

## Verification

The first guarded long-caption run retired two unlisted proxies when the
death screen closed and had zero native faults through clean exit. Its full
respawn still failed the BUG-9011 growth check with an experimental cinematic
workaround; it is not a full recovery pass. That workaround was removed.

- FAIL before this guard: build/local/m1-migration-fix-guest.log and the
  matching native crash above, after --hold-death 125.
- PASS with only this guard: build/local/m1-long-baseline-host.log and
  m1-long-baseline-guest.log, including the same 125-second death caption.
  Both players confirmed the real death screen, hatched, completed native
  migration and walked as their original species. Shared DNA was 65, 58, 52.
- PASS fresh standard-timeout repeat: m1-confirm-host.log and
  m1-confirm-guest.log. Both games reported zero faults and exited cleanly.
- PASS m1-checkpoint-before-save.log, m1-checkpoint-rejoin.log and
  m1-checkpoint-cold.log: native save, guest reconnect and full process
  restart retained distinct walking adults and 52 DNA on both games.
  The cold restart used the original DuoCellHost campaign identity.

The final guest native log records retirement of unlisted proxies when the
death caption closes. These tests do not establish general lifetime safety
for every cached object or prove that BUG-9011 had the same root cause.

## Fix

Run BUG-0300's live noun-list check before every Creature proxy movement pass
and stale removal. One list scan serves all proxies in the frame. An empty
list also retires the cached pointers. npc.proxy_listed_frame off restores
the one-second schedule. The guard does not change Tribal simulation.

## History

- 2026-11-03: opened from the isolated native crash; later-stage work remains parked.
- 2026-11-03: the guarded native sequence and persistence checks passed.
