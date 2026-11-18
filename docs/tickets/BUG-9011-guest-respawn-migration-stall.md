---
id: BUG-9011
title: The guest's post-death hatch is held by the native species migration cinematic
status: not-reproduced
kind: bug
stage: Creature
reported: 2026-11-03
source: Full Duo Creature recovery acceptance check
role: guest
versions: 0.7.3.6-duo9 development
fixed_in:
branch: duo-species
commits:
tests: tools/tests/duo_creature_rescue.py
signature: FBB3661D playing, age 0 and input_lock after a guest death
docs: docs/re/creature.md#creature-duo-recovery-departure
related: BUG-0014, BUG-9010, BUG-9012
---

## Report

The distinct-species Vantiegan pair completed the revive and expiry debit.
The guest's real death screen was confirmed through its enabled native button.
The native avatar entered age 0 with half maximum health. It never returned to
adult control within 65 seconds. FBB3661D remained active with input_lock.
The screen held a nest cinematic without a migration caption or confirmation.
Neither game reported native faults. The host had already completed its own
migration caption and Minimap tutorial before this guest test began.

## Evidence

- FAIL build/local/m1-full-rescue-guest.log in DuoQAM1FullRecovery4/5.
- Native guest species remained 291447DD and the shared DNA changed 58 to 52.
- FBB3661D is documented as CRG_SpeciesMigrate, not the egg-fade cinematic.
  The existing hatch helper skips all active cinematics.

## Follow-up verification

The proposed scene-completion workaround failed its native growth test and
was removed. See docs/re/creature.md#creature-duo-guest-migration for the
failed experiment and its corrections. No forced cinematic completion,
native Update, forced growth or NPC synchronization toggle is part of the fix.

After BUG-9012's per-frame proxy lifetime guard, two fresh host-first runs
completed the guest's actual death, migration caption, hatch and walking
within the original 65-second hatch limit. Evidence:
build/local/m1-long-baseline-host.log, m1-long-baseline-guest.log,
m1-confirm-host.log and m1-confirm-guest.log. The first pair also passed a
125-second death-caption delay, save, reconnect and cold restart. Both pairs
exited with zero native faults.

The symptom is not reproduced on this build. A causal link to BUG-9012 is
unproven, so this ticket is not marked fixed or duplicate. Retain the native
caption/growth timeout in the regression and reopen if that symptom returns.
