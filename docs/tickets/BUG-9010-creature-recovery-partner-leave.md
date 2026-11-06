---
id: BUG-9010
title: A departing guest cancels the host's Creature death timer
status: fixed
kind: bug
stage: Creature
reported: 2026-11-03
source: Duo first playable milestone interruption check
role: host
versions: 0.7.3.6-duo9 development
fixed_in: 0.7.3.6-duo9
branch: duo-species
commits: 58f8882
tests: tools/tests/duo_creature_disconnect.py
signature: a guest leaves while the host is downed and the timer disappears
docs: docs/re/creature.md#creature-duo-recovery-departure
related: BUG-9005
---

## Report

In the isolated Vantiegan Creature pair, separate the partners, knock down the
host and leave from the guest. Within one second the host no longer reports
Downed. Its shared balance remains 65 DNA instead of completing the timer.
The same test must preserve the active timer and apply exactly one expiry debit.

## Evidence

- FAIL build/local/m1-disconnect-before.log on checkpoint e0aeadf.
- Both games started from copied DuoQALandSave profiles, with different native
  species and matching shared DNA and parts before the check.

## Investigation

The player-leave callback resets the entire Creature rescue module. The frame
loop also resets it whenever the party no longer has exactly two players.
Neither operation distinguishes an existing recovery from starting a new one.

## Fix

Keep ticking an already-started recovery in the same host world after a guest
leaves. Detach a departing victim's deadline and name from its reusable seat.
Apply the expiry debit from the normal frame and send no old result to the
new occupant. The native avatar and world lifetime checks remain unchanged.
The switch is creature.recovery_leave.

## Verification

- PASS build/local/m1-final-disconnect-host.log with the final code. The host
  remained downed, charged 52 to 46 DNA once, showed the native death caption,
  hatched, grew up and walked with its original species 291447DA.
- PASS build/local/m1-final-disconnect-rejoin.log. The guest left while downed
  and rejoined under a replacement name before expiry. Both games reached
  41 DNA from 46 and the replacement had no inherited knockout. Zero faults.
- FAIL build/local/m1-final-disconnect-nofix.log with the switch off. The host
  timer disappeared immediately after the guest left, matching the report.
- Full normal guest respawn also exposed independent BUG-9011. That failure
  remains a milestone blocker and is not counted as a passing recovery run.
