#!/usr/bin/env python3
"""Tribe: a member the guest buys at its hut is born in the host's game and is the guest's (BUG-0671).

A left click on the own hut costs food 10 (the game's price) and a baby is born 5-8 s later (the stage's message
0x63BBC42 -> SporeCoop::TribeStage_BabyBorn 0xCD74A0, which spawns the member). On a guest that member was one of
its own game only: the mirror reconcile removed it and the host's game never had it.

1. Two games on the tribe fixture; the host's tribe at level 2 (cap 12); the host's game gets food 60 (the shared
   food gives the guest's game the same).
2. The guest's player left-clicks its own hut, as a player does (the mouse, after hovering).
3. Up to 20 s: the members of the host's player tribe (the game's own list) and the guest's game's own list.
PASS: the host's game has one member more, the guest's game shows it (the same count), the new member is the
guest's (its owner in the host's split), food went down by the price once on the host.

SPORE_FIX_OFF=tribe.guest_baby: the host never gets a member (FAIL).

  SPORE_LANE=5 python3 tools/tests/coop_tribe_guest_baby.py
"""
import re
import time

from twoinst import Checks, start


def player(con):
    """(members, tribe pointer) of the game's player tribe"""
    for ln in con.call('coop.tribe')['out'].splitlines():
        if '(player)' in ln:
            m = re.search(r': (\d+) members .*obj=(?:0x)?([0-9A-Fa-f]+)', ln)
            return (int(m.group(1)), int(m.group(2), 16)) if m else (-1, 0)
    return (-1, 0)


def owners(con):
    """{member id: owner} of the host's player tribe"""
    out, inside = {}, False
    for ln in con.call('coop.tribe')['out'].splitlines():
        if ln.startswith('tribe '):
            inside = '(player)' in ln
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+)', ln)
        if m and inside:
            out[int(m.group(1))] = int(m.group(2))
    return out


def food(con):
    m = re.search(r'food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def hut(con):
    for line in con.call('coop.targets objs')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'obj' and f[1].startswith('hut') and f[1].endswith('p'):
            return int(f[3]), int(f[4])
    return None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe level 2')['out'].strip(), flush=True)
    n0, pt = player(host)
    f = food(host) or 0
    print('  food:', host.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt:X} {int(60 - f)}')['out'].strip()
          .splitlines()[0], flush=True)
    time.sleep(4)
    o0 = owners(host)
    f0, gf0 = food(host), food(guest)
    print(f'  before: host {n0} members food {f0} | guest {player(guest)[0]} members food {gf0}', flush=True)
    ck(gf0 is not None and gf0 >= 10, f'the guest\'s game has the food for a member ({gf0})')
    h = hut(guest)
    ck(h is not None, f'the guest\'s own hut is on its screen ({h})')
    if not h:
        return ck.result()
    guest.call(f'move {h[0]} {h[1]}')
    time.sleep(0.6)
    guest.call(f'click {h[0]} {h[1]}')
    t0, hn, gn = time.time(), n0, -1
    while time.time() - t0 < 20:
        time.sleep(1)
        hn, gn = player(host)[0], player(guest)[0]
        if hn > n0 and gn == hn:
            break
    time.sleep(2)
    hn, gn = player(host)[0], player(guest)[0]
    f1 = food(host)
    print(f'  after {time.time() - t0:.0f} s: host {hn} members food {f1} | guest {gn} members food {food(guest)}', flush=True)
    print('  host', host.call('coop.tribe anims')['out'].splitlines()[-1], '| guest',
          guest.call('coop.tribe anims')['out'].splitlines()[-1], flush=True)
    new = {i: o for i, o in owners(host).items() if i not in o0}
    print(f'  new members on the host: {new}', flush=True)
    ck(hn == n0 + 1, f'the host\'s game has the member the guest bought ({n0} -> {hn})')
    ck(gn == hn, f'the guest\'s game shows it ({gn} of {hn})')
    ck(len(new) == 1 and all(o != 0 for o in new.values()), f'the new member is the guest\'s ({new})')
    ck(f0 is not None and f1 is not None and abs((f0 - f1) - 10) < 0.5, f'the host\'s food went down by the price once ({f0} -> {f1})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
