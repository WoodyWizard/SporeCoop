---
id: BUG-0736
title: Creature: a guest's own Summon Flock is made in its own game only (the host never makes the flock)
status: fixed
kind: bug
stage: Creature
reported: 2026-11-08
source: BUG-0418 sibling list, K2b audit 2026-11-08
role: guest
versions: 
fixed_in: 0.7.11
branch: fix/w46-k2b-content-b
commits: 8911e865, e896b4a8
tests: flock_relay, flock_relay_slow (coop_party_defence.py --flock)
signature:
docs: docs/re/creature.md#creature-player-species-callers, docs/known-issues.md#issue-flock-follows-host
related: BUG-0418, BUG-0486, BUG-0501, BUG-0556
---

## Report
K2b audit of the class BUG-0418 (docs/re/creature.md:2699, :2819): a guest's own Summon Flock (ability 0x33) was made in its own game only: the host never had the flock, the creatures were the guest's game's own.

## Root cause
Observed (lab 20261108-040900, `SPORE_FIX_OFF=creature.flock_relay`): the guest's ability tick started its own summon event (Drama Manager kind 8: `summon started=1` on the guest), the host's pack stayed empty. Static: SummonEvent_Start 0xD50EA0 places a herd of the event's species 70 m from the local avatar and adds its members to the local pack; SummonEvent_Update 0xD512A0 (each frame) keeps the hearts and, when it ends, sends the members after the avatar.

## Fix
`creature.flock_relay`: the guest's game does not start event kind 8 (drama.cpp); the first tick of its ability 0x33 (BT_SpecialAbilityTick, the roar relay's hook) sends K_FLOCK; the host starts the same event with the guest's puppet as cGameNounManager +0x54 (start and update, with the avatar's tuning lent to a puppet without one, as the AI swap): the herd is made 70 m from the guest's creature in the shared world and joins the party's pack (the host's; the guests show its members as NF_PACK allies, BUG-0556). The pack follows the host's creature afterwards (one posse per world): docs/known-issues.md#issue-flock-follows-host. `coop.fix creature.flock_relay off`: the guest's own event (0.7.10).

## Verification
FAIL: out/lab/20261108-040900. PASS: out/lab/20261108-034334 (flock_relay: the host's pack 0 -> 3, the new members 66-84 m from the guest's creature and 211-228 m from the host's, the guest's own event refused), out/lab/20261108-041412 (the _slow twin at 20 fps over the player network).
Re-run on the merged branch fix/w46-k2b-content (the run dirs above were in the sub-worktrees fix/w46-k2b-content-a / -b, removed 2026-11-08; their ids stay as the record): FAIL out/lab/20261108-070150, PASS out/lab/20261108-054842.

## History
- 2026-11-08: opened.
- 2026-11-08 (fix/w46-k2b-content-b): fixed, `creature.flock_relay`; FAIL and PASS runs above.
- 2026-11-08: FAIL / PASS runs repeated on the merged K2b branch.
- 2026-11-08: released in 0.7.11.
