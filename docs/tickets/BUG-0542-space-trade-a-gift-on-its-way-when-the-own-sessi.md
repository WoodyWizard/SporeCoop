---
id: BUG-0542
title: Space trade: a gift on its way when the own session ends comes back although the partner has it (doubled)
status: fixed
kind: bug
stage: Space
reported: 2026-11-04
source: BUG-0196 (open end of space_trade.cpp)
role: both
versions: 0.7.5 and earlier
fixed_in: 0.7.6
branch: fix/w42-space-play
commits: ad8eaead
tests: space_trade_end
signature:
docs: docs/re/space.md#space-play-w42
related: BUG-0196, BUG-0058, BUG-0225
---

## Report
Listed in BUG-0196: "a gift or sale on its way when the own session ends comes back (the sender cannot know
whether the partner already got it): leaving right after Give can duplicate the charges". A player who gives
tool charges (or sells spice to a partner colony) and leaves, or whose connection drops, before the answer is
back gets them back while the partner keeps them.

## Root cause
Observed (lab 20261104-112321): the receiver adds a gift in the frame it arrives (only a rival offer waits for a
click); `on_session_end` refunded every unanswered gift. The host had the 5 charges, the guest had 12 again.

## Fix
`net::Connection::next_rel_id` / `rel_acked(lo, hi)`: each gift / sale remembers the reliable message that
carried it, and the trade tick records when the partner's side acknowledged it. At the own session end an
acknowledged gift or sale is not given back (a sale pays the export bonus as its answer would); an offer
(rivals) and an unacknowledged message come back as before. Switch `coop.fix trade.delivered off`.
Not covered: what did not fit on the receiver (the "back" part of a lost answer) is lost; a partner who leaves
right after receiving (its BYE may overtake its answer) is still refunded by the "partner left" path.

## Verification
- FAIL (trade.delivered off): out/lab/20261104-112321 (the guest's bomb 12: doubled).
- PASS: out/lab/20261104-112227.

## History
- 2026-11-04: opened and fixed on fix/w42-space-play.
- 2026-11-04: released in 0.7.6.
