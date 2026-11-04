#!/usr/bin/env python3
"""BUG-0486: the game's random UFO visit (Drama Manager event kind 2, cUFOAbductionEvent) ran in each
game by itself: a guest saw a UFO the host never had (the detector's noun_only_guest:ufo /
guest_only:ufo), and that UFO took the guest's mirrors of the host's creatures in the guest's game only.
The host owns the world: a guest's own Drama Manager starts no UFO visit (creature.guest_events).

The test starts every idle UFO event the game's own way (`coop.drama start 2`: the Drama Manager's
StartKind, as the stage start does for its events) and counts the UFOs in the game's object list:
  1. the guest: no UFO of its own, the event stays inactive (SPORE_FIX_OFF=creature.guest_events FAILS)
  2. the host (control: the command makes a UFO where it may): its UFO is there

  SPORE_LANE=N python3 tools/tests/coop_drama_guest.py
"""
import re
import sys
import time

import worlddiff
from twoinst import Checks, start

UFO = next(k for k, v in worlddiff.NOUNS.items() if v == 'ufo')


def ufos(con):
    return worlddiff.dump(con).nouns.get(UFO, 0)


def ufo_line(con):
    out = con.call('coop.drama')['out']
    m = re.search(r'ufo +started=(\d+) refused=(\d+) event=\S+ active=(-?\d+)', out)
    return out, (tuple(int(x) for x in m.groups()) if m else None)


def main():
    host, guest = start(fixture='creature')
    ck = Checks()
    time.sleep(3)
    g0, h0 = ufos(guest), ufos(host)
    print(f'  ufos before: host {h0}, guest {g0}', flush=True)

    out = guest.call('coop.drama start 2')['out']
    print('  guest: ' + out.strip().replace('\n', ' | '), flush=True)
    time.sleep(4)
    g1 = ufos(guest)
    _, st = ufo_line(guest)
    print(f'  guest ufos after its start: {g1}; ufo event (started, refused, active) {st}', flush=True)
    ck(st is not None, 'guest: the UFO event exists in its Drama Manager (the start reached it)')
    ck(g1 <= g0, f'guest: no UFO of its own appears ({g0} -> {g1})')
    ck(st is not None and st[2] == 0, f'guest: its UFO event stays inactive ({st})')

    out = host.call('coop.drama start 2')['out']
    print('  host: ' + out.strip().replace('\n', ' | '), flush=True)
    time.sleep(4)
    h1 = ufos(host)
    _, hs = ufo_line(host)
    print(f'  host ufos after its start: {h1}; ufo event {hs}', flush=True)
    ck(h1 > h0 and hs is not None and hs[2] == 1, f'host (control): the start makes its UFO ({h0} -> {h1}, {hs})')
    g2 = ufos(guest)
    print(f'  guest ufos while the host has one: {g2} (the host\'s UFO is not shown on a guest: BUG-0486 rest)',
          flush=True)
    host.call('coop.drama')  # keep the log line
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
