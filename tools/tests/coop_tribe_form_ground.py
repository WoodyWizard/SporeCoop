#!/usr/bin/env python3
"""Tribe: the ground of an NPC village the host's game forms during play is the host's in the guest's game (BUG-0332).

In 4-player campaigns (campaign_ct, lab 20261105-233903 and 20261106-000530 of fix/w45-tribe) the host's game formed
an NPC tribe during Tribe play (cNpcTribeController); the guests made their copy of it (tribe.npc_tribes,
SpawnNpcTribe at the host's position) and the logs said "made it ... at 5.1 m" / "at 5.9 m": the guests' ground
there was 5.1 / 5.9 m higher than the host's, and the host's members that went there stood 5 m higher on the guests.

1. Two games on the tribe fixture.
2. The host's game forms its next NPC tribe (`coop.tribe form`: the controller's own forming, as in play); the guest's
   game makes its copy of it (tribe.npc_tribes).
3. On both games the game's own ground (`coop.ground`, cPlanetModel::GetHeightAt) at the new village's centre and on rings of 10, 25 and 50 m round it (8 directions),
   twice 10 s apart, and the distance of the guest's village from the host's.
PASS: the two grounds within 50 m of the new village differ by less than LIMIT, the guest's village within 2 m.

SPORE_FIX_OFF=<the fix> must FAIL (docs/tickets/BUG-0332).
--rejoin (BUG-0947): then the guest leaves and joins again (it loads the host's world, the new village in it) and
the two grounds round the village are compared again (a four-player campaign's rejoin in Tribe had them 4.5-4.9 m
apart there).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_form_ground.py
"""
import argparse
import math
import re
import time

from coop_tribe_village_ground import compare, ground, ring_points
from twoinst import Checks, start

LIMIT = 0.5  # m


def tribes(con):
    """[(player, (x y z))] of the game's tribe list"""
    return [(m.group(1) is not None, tuple(float(m.group(i)) for i in (2, 3, 4))) for m in re.finditer(
        r'^tribe \d+( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe')['out'], re.M)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rejoin', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    before = tribes(host)
    print(f'  before: host {len(before)} tribes, guest {len(tribes(guest))}', flush=True)
    # tribe.form_pace (BUG-0571) holds the forming until the party's food is over 100 x players
    print('  host: ' + host.call('coop.res add food 250')['out'].strip()[:120], flush=True)
    time.sleep(2)
    print('  host: ' + host.call('coop.tribe form')['out'].strip()[:300], flush=True)
    t0, new = time.time(), None
    while time.time() - t0 < 20 and not new:
        new = [p for pl, p in tribes(host) if not pl and all(math.dist(p, q) > 5 for _, q in before)]
        time.sleep(1)
    ck(bool(new), 'the host\'s game formed a new NPC tribe')
    if not new:
        return ck.result()
    at = new[0]
    print(f'  the new village on the host: ({at[0]:.1f} {at[1]:.1f} {at[2]:.1f}), r {math.dist(at, (0, 0, 0)):.2f}, '
          f'ground {ground(host, at)}', flush=True)
    t1, mine = time.time(), None
    while time.time() - t1 < 30 and not mine:
        mine = sorted((math.dist(p, at), p) for pl, p in tribes(guest) if not pl and math.dist(p, at) < 40)
        time.sleep(1)
    ck(bool(mine), 'the guest\'s game has a village for the host\'s new one')
    if mine:
        d, p = mine[0]
        print(f'  the guest\'s village: ({p[0]:.1f} {p[1]:.1f} {p[2]:.1f}), r {math.dist(p, (0, 0, 0)):.2f}, '
              f'{d:.2f} m from the host\'s', flush=True)
    worst = 0.0
    pts = ring_points(at)
    for k in range(2):
        w = compare(host, guest, [(dd, q) for dd, q in pts if dd <= 50])
        print(f'  sample {k + 1}: the largest |host - guest| ground per ring: '
              + ', '.join(f'{dd} m: {v:.2f}' for dd, v in sorted(w.items())), flush=True)
        print(f'    centre: host {ground(host, at)} guest {ground(guest, at)}', flush=True)
        worst = max([worst] + list(w.values()))
        time.sleep(10)
    ck(worst < LIMIT, f'the guest\'s ground round the host\'s new village is the host\'s (largest difference within '
       f'50 m {worst:.2f} m < {LIMIT})')
    ck(mine and mine[0][0] < 2.0, f'the guest\'s village stands where the host\'s does '
       f'({mine[0][0] if mine else -1:.2f} m < 2)')
    if a.rejoin:  # BUG-0947
        from coop_tribe_looks import rejoin
        if rejoin(ck, host, guest):
            time.sleep(15)
            worst2 = 0.0
            for k in range(2):
                w = compare(host, guest, [(dd, q) for dd, q in pts if dd <= 50])
                print(f'  after the rejoin, sample {k + 1}: ' + ', '.join(f'{dd} m: {v:.2f}' for dd, v in sorted(w.items())),
                      flush=True)
                print(f'    centre: host {ground(host, at)} guest {ground(guest, at)}', flush=True)
                worst2 = max([worst2] + list(w.values()))
                time.sleep(10)
            ck(worst2 < LIMIT, f'after the guest\'s rejoin its ground round the village is still the host\'s (largest '
               f'difference within 50 m {worst2:.2f} m < {LIMIT})')
            if worst2 >= LIMIT:  # BUG-0947 (exploration, printed): the stamps there, and what a re-stamp does
                near = {}
                for con, who in ((host, 'host'), (guest, 'guest')):
                    near[who] = []
                    for ln in (con.call('coop.terrain list').get('out') or '').splitlines():
                        f = ln.split()
                        if len(f) > 7 and f[0] == 's' and math.dist(tuple(map(float, f[4:7])), at) < 80:
                            near[who].append(f)
                            print(f'    {who} stamp: {ln.strip()[:150]}', flush=True)
                for f in near['guest']:
                    if f[1] == '770':
                        print('    guest reapply:', guest.call(f'coop.terrain reapply {f[2]} {f[2]}')['out'].strip()[:200],
                              flush=True)
                time.sleep(4)
                w = compare(host, guest, [(dd, q) for dd, q in pts if dd <= 50])
                print('  after the guest re-stamped them live: ' + ', '.join(f'{dd} m: {v:.2f}' for dd, v in sorted(w.items())),
                      flush=True)
                print('    guest:', guest.call('coop.terrain rebuild')['out'].strip()[:120], flush=True)
                time.sleep(6)
                w = compare(host, guest, [(dd, q) for dd, q in pts if dd <= 50])
                print('  after a rebuild on the guest: ' + ', '.join(f'{dd} m: {v:.2f}' for dd, v in sorted(w.items())),
                      flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
