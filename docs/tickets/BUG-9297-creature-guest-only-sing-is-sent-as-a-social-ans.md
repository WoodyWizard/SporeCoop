---
id: BUG-9297
title: Creature (guest): only sing is sent as a social answer, so the impress bar never fills
status: fixed
kind: bug
stage: creature
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in: 0.7.14.1-lineages68-dev
branch: fix/l47-impress
commits: 0e2a2801, 9d64f85d
tests: tools/qa_win (live two-game QA on the MSI and TITAN, 2026-11-21, switch off and on)
signature:
docs: docs/re/creature.md#creature-social-prompts
related: BUG-0179, BUG-0518, BUG-0525, BUG-9136, BUG-9258
---

## Report
Live 67-dev, Creature, a guest: "The impress bar was still not functioning. No filling animation, and they couldn't add
creatures to their pack. Tested on Host's species, as well as their own." Every social game of the guest ended "the
prompt list is through and the meter not full" on the host (npc 38 twice, npc 73, npc 66); nothing was impressed, so
nothing could join the pack (no guest_pack recruit line on the host).

## Root cause
Observed in the code, inferred as the cause (the logs fit, the ability kinds are not seen at runtime yet):
social.cpp `social_ability` (keys 1-4) and `h_BarUse` (the ability bar) treat an ability as social only when its +0x10 is
1 (`field<uint32_t>(a, 0x10) == 1`). +0x10 is the social kind the prompt must match (docs/re/creature.md#creature-social-prompts),
so 1 is only the sing; the guest's dance, charm and pose (+0x10 2..4 presumably) go down the game's own path and are
never sent as K_ACT answers. Prompts asking for another kind can never be answered; a sing on them counts 0.1.
player_rules.cpp `social_use` already uses `+0x10 != 0`. The test fixture creature has only the sing, so the tests (which
also answer through `coop.social act`) never saw it.
Second, confirmed: the guest log's "own attack ability 4 type 0x25 ... social: no hit (BUG-9136)" lines come every
~5.3 s and 0.8 s after each fail: the avatar's own automatic sing, not an answer; correctly not sent.
"Host's species": npc 38 is the host's own creature (relation 6, allied); the host played it in social mode 4 against
the host's avatar (`social_mode()` uses CreatureSocialMode(npc) with the host's avatar). Whether a partner's species can
be recruited is a gameplay decision (asked as 7L), not this bug.

## Fix
Done (fix/l47-impress, switch `social.all_kinds`, `coop.fix social.all_kinds off` = 67-dev): social.cpp `is_social()`
takes any ability with +0x10 != 0 as social (combat abilities have 0, docs/re/creature.md#creature-social-prompts); used
by `social_ability` (keys 1-4, `coop.social act <id> <slot>`) and `h_BarUse` (the bar's buttons). Log lines (60 a run):
guest "social: act sent to npc N: ability I type T kind K"; host "social: player P answers npc N: type T kind K to
prompt I (type T kind K): match | another kind, score S of NEEDED".
Observed: the code (`== 1` in both places, `!= 0` in player_rules). Inferred: the guest's other social abilities have
+0x10 2..4 (not seen at runtime; the fixture creature has only the sing).

## Verification
no-fail-run: pending the lead's two-player QA (built only; a creature with a second social kind is needed, the
fixture has only the sing). Test plan in the fix/l47-impress report.
- Live QA 2026-11-21 (lead, two games on the MSI: host MSITribeHost instance 2, guest MSIGuest instance 1; the Creature part first with the host on TITAN QA 9): PASS (on): host log "player 1 answers npc 22: type 00000023 kind 4 to prompt 0 (type 00000023 kind 4): match, score 2.0 of 8.0", then 3.0 of 8.0; kind 2 answers arrived too ("another kind"). The guest creature has kinds 4, 1, 2 (`coop.social abil -1` key column). Off-run not done (67-dev sent only kind 1, the report). The third prompt asked for a kind the test creature lacks: no win.

## History
- 2026-11-20: opened.
- 2026-11-20: fix on fix/l47-impress (built, not run in game).
- 2026-11-21: live two-game QA (see Verification).
- 2026-11-21: set to fixed for 68-dev: merged into feat/lineages-47 with the live FAIL / PASS in Verification (any open part stays in docs/known-issues.md).
- 2026-11-21: released in 0.7.14.1-lineages68-dev.
