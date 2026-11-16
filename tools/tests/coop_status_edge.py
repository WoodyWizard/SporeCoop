#!/usr/bin/env python3
"""Creature: the overlay's status block (top right) stays inside the screen on both games (BUG-1219).

The block starts at x = screen width - 480; in Creature it has the pack line of teamplay ("Pack: Alice close (regen)
|  rescue ready  |  joint hits 0, rescues 0  |  hits host 0, partners 0", ~100 letters, ~890 px by the overlay
font's estimate): the line ran off the right edge (0.7.15 key frames: every Creature frame with a partner, both
games). Fixed: the line is cut between its fields into lines that end before the edge.

1. Two games in Creature (the creature fixture), connected.
2. On both games `coop.status` "status block (...) drawn to x <right> of <screen width>": the block is drawn (its
   rect has a height) and its widest drawn line ends inside the screen (right <= width).
3. A key frame of both games (the block top right: every line readable, nothing cut at the edge).
Without the fix (SPORE_FIX_OFF=core.status_wrap) step 2 FAILS: the pack line ends ~400 px past the edge.

  SPORE_LANE=3 python3 tools/tests/coop_status_edge.py
"""
import re
import time

from twoinst import Checks, game_runs, keyframe, start


def block(con):
    out = con.call('coop.status')['out']
    m = re.search(r'status block \((-?\d+) (-?\d+) (-?\d+) (-?\d+)\) drawn to x (-?\d+) of (\d+)', out)
    pack = re.search(r'^teamplay .*$', out, re.M)
    return (tuple(int(v) for v in m.groups()) if m else None), (pack.group(0) if pack else '')


def main():
    host, guest = start('creature')
    ck = Checks()
    game_runs(host, 90)
    game_runs(guest, 90)
    time.sleep(3)
    for role, con in (('host', host), ('guest', guest)):
        b = None
        for _ in range(20):  # the block of a frame with the pack line (connected, in Creature, no menu)
            b, _ = block(con)
            if b and b[3] > b[1] + 60:
                break
            time.sleep(0.5)
        print(f'  {role}: status block {b}', flush=True)
        ck(b is not None and b[3] > b[1], f'the {role}\'s status block is drawn ({b})')
        if b:
            x1, y1, x2, y2, right, sw = b
            ck(sw > 0 and right <= sw,
               f'the {role}\'s status block ends inside the screen (widest line to x {right}, screen {sw})')
    keyframe('status_block', 'creature: the status block top right on both games: the "Pack: ..." line in two lines, '
                             'nothing cut at the right edge')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
