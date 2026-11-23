---
id: BUG-9342
title: Social Menu: a galaxy player's place shows the era key (Abber|Creature) instead of Abber - Creature
status: fixed
kind: bug
stage: menu
reported: 2026-11-22
source: TITAN QA run 2026-11-22 (Social Menu shots)
role: 
versions: 0.7.14.1-lineages69-dev (alpha)
fixed_in: unreleased
branch: feat/lineages-alpha
commits: 28034591
tests: tools/qa_win (manual, 2026-11-22): TITAN, QA 9 on Samuth (Creature), QA 8's Social Menu (F8); coop.fix ui.social_era_names off/on
signature:
docs: docs/12-server-and-players.md#server-social-menu
related: BUG-9334
---

## Report
TITAN QA 9, the Social Menu (F8) in the galaxy menu: a player of the galaxy on Abber's Creature era showed
"Abber|Creat..." under the name (the server's era key) where the menu writes "Samuth  -  Creature" elsewhere.

## Root cause
Inferred from the code: coop.cpp social_tick passes the PLAYERS entry's planet (the era key, "<planet>|<Era>") to
where_text as it is; net::galaxy::era_display would make it "Abber (Creature)" or the planet and its stage.

## Fix
coop.cpp where_text: a place that is an era key shows the planet (a crossing's planet for "<planet>~<party>|<Era>")
and its stage ("Civilization" for Civ) as "Samuth  -  Creature"; when that does not fit beside the row's buttons the
Social Menu's existing rule shows the planet alone. Switch `coop.fix ui.social_era_names off`: the key as it is.

## Verification
TITAN, the rebuilt test galaxy: QA 9 entered Samuth (Creature); QA 8 in the galaxy menu opened the Social Menu (F8):
- PASS (on): QA 9's row in "Online now" reads "Samuth" (the planet alone: "Samuth  -  Creature" does not fit beside
  Invite and Whisper) (images/bug9342_social_on_off.png, left).
- FAIL (off): "Samuth|Cre..." (the same picture, right).
no-fail-run: Windows QA by hand; the runs above are the switch on and off.

## History
- 2026-11-22: opened.
- 2026-11-22: fixed on feat/lineages-alpha.
