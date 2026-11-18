---
id: BUG-9159
title: Tribe: a joiner without a creature goes into the creature editor first; the village follows their save (decision 2M)
status: open
kind: feature
stage: tribe
reported: 2026-11-12
source: owner
role: guest
versions: 0.7.8-lineages54-dev
fixed_in:
branch: feat/lineages-47
commits:
tests: tests/tribe_late_tests.cpp (host); tools/qa_win (manual, three games on the MSI 2026-11-12)
signature:
docs: docs/re/tribe.md#tribe-editor-first
related: BUG-9135 BUG-9150 BUG-9137
switch: tribe.late_editor_first
---

## Report
Owner decision 2M (2026-11-12): a joiner without a creature of their own goes into the creature editor first; their village is founded when they save it, with the loading screen of decision 1P A. The stand-in village (BUG-9150) stays only as the fallback when they leave the editor without saving.

## Root cause
Design. Before: a joiner without a creature got a stand-in village at once (BUG-9150) and could make a creature from the Co-op window (tribe.late_create_editor, off, never run). Two tries that let the guest decide by itself failed live: 5 s after the world a guest with a village of its own was sent into the editor (its members were not known yet), and after the 15 s settle a new joiner was too late (its member state came 33 s after the join and the host had made the stand-in). Now the host decides: it knows who has no seat and no creature of their own.

## Fix
tribe.late_editor_first (on), tribe.late_create_editor now on. Host: a partner whose wait would be StandIn is sent MSG_TRIBE_LATE (84, u8 1, u32 world) and waits as Editing (tribe_late::editor_ask): 20 s for the answer, then the stand-in. Guest: on the message it opens the creature editor for a new creature (coop.cpp open_new_creature_editor, outside the frame, up to 10 tries while the game is busy) and sends CMD_LATE_EDITING 1; back from the editor, creations.cpp sends 2 (saved: the host holds a minute for the creature key) or 0 (none: the stand-in now). The host tells everyone once "<name> is making a creature of their own. Their village is founded when they save it." While their own creature is chosen and the village is coming, the guest sees the loading screen (coop.cpp on_overlay, ring_timer::loading): the frame blurred and dimmed, a gold ring filling over about 20 s with a turning light, "Founding your village" and the steps "Catching up to <host>...", "Matching tools...", "Buildings are rising...", "Final touches...". `coop.tribe late` on the host now lists each unseated partner's wait and why the founding did nothing. Off: the stand-in at once, as before.

## Verification
no-fail-run: a feature. Live 2026-11-12, three games on the MSI (host on a copy of a Tribe save with 2 seats, the MSI guest with its village, a new joiner on an empty profile): the host asked ("has no creature of their own: asked to make one in the editor"), the joiner's editor opened 1 s later, the host held the founding ("is making a creature of their own"), the joiner saved a creature (a body and a mouth, named), the loading screen showed, 15 s after the save its village was founded with its own species (seat 3, no stand-in), the catch-up ran; 0 faults on all three games. The MSI guest with a village was not sent into the editor. Host tests: tests/tribe_late_tests.cpp (editor_ask, editor_first, loading_progress / loading_step). Not seen: leaving the editor without saving (the stand-in after it), the Civ part of 1P.

## History
- 2026-11-12: opened.
