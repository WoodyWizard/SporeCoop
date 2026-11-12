#!/usr/bin/env python3
"""BUG-0796: a partner's animation is never played on its cell (Cell) or its marker (Tribe).

MSG_ANIM is reliable and comes after a stage switch too: on_remote_anim checked the game's object list only for a
Creature puppet, any other puppet went to cCreatureBase::PlayAnimation, which reads the animated creature at +0xB54
of a cCellObjectData in the cell pool or of a 40-byte heap marker.

Two games on the cell fixture (--fixture tribe: the tribe fixture). On the guest, the host's MSG_ANIM arrives
(`coop.overlay animrx 0 <anim> 5`: on_remote_anim as the message handler calls it) while the guest's game shows the
host as a cell / a marker. PASS: nothing is played on it (played=0) and no game faulted.
SPORE_FIX_OFF=core.anim_creature_only must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_anim_not_creature.py [--fixture cell|tribe]"""
import argparse
import re
import time

from twoinst import Checks, start

ANIM = '0F14FAA4'  # (any id: the guard is on the puppet's kind, not on the animation)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='cell', choices=('cell', 'tribe'))
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    time.sleep(5)
    out = guest.call(f'coop.overlay animrx 0 {ANIM} 5')['out'].strip()
    print('  guest: ' + out, flush=True)
    m = re.search(r'puppet ([0-9A-Fa-fx]+|\(nil\)), (\w+)\): played=(\d+)', out)
    ck(m and m.group(2) in ('Cell', 'Tribe'), f'the guest shows the host as a {a.fixture} puppet ({m and m.group(2)})')
    time.sleep(3)
    ck(m and m.group(3) == '0', f'no animation was played on it (played={m and m.group(3)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
