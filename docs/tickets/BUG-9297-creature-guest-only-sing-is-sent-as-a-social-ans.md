---
id: BUG-9297
title: Creature (guest): only sing is sent as a social answer, so the impress bar never fills
status: open
kind: bug
stage: creature
reported: 2026-11-20
source: player report 2026-11-20 (a guest, 67-dev, report 20261120-205700, with the host log)
role: guest
versions: 0.7.14.1-lineages67-dev
fixed_in:
branch:
commits:
tests:
signature:
docs:
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
Planned: `== 1` -> `!= 0` in both places (switch `social.all_kinds`), plus log lines: guest "act sent (type, kind)",
host "answer kind X to prompt kind Y, match, score/needed". Test: a fixture creature with two social kinds; a real bar
click on a non-sing ability answering a non-sing prompt: FAIL (no answer counted) with the switch off, PASS on.

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-20: opened.
