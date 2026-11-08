#!/usr/bin/env python3
"""Tribe: the guest's copy of a tribe the host's game formed uses up the same spawn marker (BUG-0741).

The host's NPC tribe controller (FormTribe 0xCC6440) forms a tribe at the nearest tribe spawn marker
(cGameplayMarker, +0x108 == 0x57F09E5E) and destroys that marker; the guest makes its copy of the
host's new tribe (tribe.npc_tribes, BUG-0381) without FormTribe, so its game kept the marker: the
Tribe detector's unpaired gameplay marker (1 guest / 0 host). A guest controller that ever formed
tribes could then form one on a site already used.

1. Two games on the tribe fixture (cold). Both games list the same tribe spawn markers (the world's).
2. The host's controller forms its next tribe (`coop.tribe form`, the function the game calls during
   play; again while the next species is still being baked; tribe.form_pace off, so the party's food
   does not hold it). The host's game has one marker fewer for each
   tribe it formed (with 3-4 tribes it forms the next ones every second).
3. Within 20 s the guest's game has a tribe where the host's new one is (its copy) and no tribe spawn
   marker within 5 m of the one the host's game used up: the guest's own object list.

SPORE_FIX_OFF=tribe.copy_marker must FAIL (the guest keeps the marker).

  SPORE_LANE=7 python3 tools/tests/coop_tribe_copy_marker.py
"""
import math
import re
import time

from twoinst import Checks, start


def markers(con):
    return [tuple(float(v) for v in m.groups()) for m in
            re.finditer(r'marker \S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe markers')['out'])]


def tribes(con):
    return [tuple(float(m.group(i)) for i in (1, 2, 3)) for m in
            re.finditer(r'^tribe \d+(?: \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe')['out'], re.M)]


def gone(before, after, r=2.0):
    return [p for p in before if not any(math.dist(p, q) < r for q in after)]


def show(ps):
    return [tuple(round(v) for v in p) for p in ps]


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(8)
    hm0, gm0 = markers(host), markers(guest)
    print(f'  markers: host {show(hm0)}; guest {show(gm0)}', flush=True)
    ck(hm0, f'the host\'s game has tribe spawn markers ({len(hm0)})')
    ck(not gone(hm0, gm0, 5) and not gone(gm0, hm0, 5), 'both games list the same tribe spawn markers at the start')
    # the co-op's forming pace (tribe.form_pace, BUG-0571) holds the next tribe until the party's food is up: off
    # here, the test is about where the tribe forms, not when
    host.call('coop.fix tribe.form_pace off')
    t0, ht0 = time.time(), tribes(host)
    while time.time() - t0 < 45:
        out = host.call('coop.tribe form')['out'].strip()
        print(f'  host: {out}', flush=True)
        m = re.match(r'tribes (\d+) -> (\d+)', out)
        if m and m.group(1) != m.group(2):
            break
        time.sleep(1.5)
    host.call('coop.fix tribe.form_pace on')  # (with 3-4 tribes the controller forms the next ones every second)
    time.sleep(1)
    hm1, ht1 = markers(host), tribes(host)
    used = gone(hm0, hm1)
    new = gone(ht1, ht0, 5)
    print(f'  host: new tribes {show(new)}, markers used up {show(used)}', flush=True)
    if not ck(used and len(used) == len(new), f'the host\'s controller formed tribes and used up a marker for each '
              f'({len(new)} new tribes, {len(used)} markers gone)'):
        return ck.result()
    copies, left, gm1, t0 = [], used, gm0, time.time()
    while time.time() - t0 < 20:
        gt = tribes(guest)
        copies = [t for t in new if any(math.dist(t, g) < 10 for g in gt)]
        gm1 = markers(guest)
        left = [p for p in gm1 if any(math.dist(p, u) < 5 for u in used)]
        if len(copies) == len(new) and not left:
            break
        time.sleep(1)
    print(f'  guest: copies of the host\'s new tribes {show(copies)}; markers {show(gm1)}', flush=True)
    print('  guest: ' + guest.call('coop.tribe markers')['out'].strip().splitlines()[-1], flush=True)
    ck(len(copies) == len(new), f'the guest\'s game has its copy of each new tribe ({len(copies)} of {len(new)})')
    ck(not left, f'the guest\'s game has no tribe spawn marker where the host\'s controller used one up ({show(left)})')
    ck(len(gm1) == len(hm1), f'the same number of tribe spawn markers on both games (host {len(hm1)}, guest {len(gm1)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
