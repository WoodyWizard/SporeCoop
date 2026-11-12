#!/usr/bin/env python3
"""Cell: a partner's poison clouds do not hurt a player's cell while friendly fire is off (BUG-0958).

The host's game makes every player's poison clouds (its own avatar's, and a guest's through puppet_poison,
BUG-0716); they are mirrored to the guests like any cell. Before the fix a guest's own game ran the contact of a
host's cloud mirror with its own cell and the guest lost hp and was knocked out without any bite (lab
20261112-070110, -062128).

--who guest (default): the HOST's cell gets the poison part (`coop.pool borrow poison graft`); the guest is put onto
the host's clouds (as its game shows them, `coop.pool see`: cell type 5) again and again for up to 40 s. Judged on
the guest's own game: its hp (the game's +0x244) never goes down and it is never knocked out, while it was put onto
at least 6 clouds. --who host: the GUEST's puppet gets the part in the host's game (its clouds are that partner's,
puppet_poison) and the host is put onto the clouds near the puppet (more than 3 m from its own cell: the host's own
cell shares the grafted model and makes clouds of its own, which are not a partner's).
SPORE_FIX_OFF=cell.partner_poison must FAIL (hp lost or knocked out).
--ff: friendly fire on (all.friendly_fire): the old behaviour; printed only.

  SPORE_LANE=6 python3 tools/tests/coop_cell_partner_poison.py [--who guest|host] [--ff]
"""
import argparse
import math
import re
import time

from coop_cell_ko_food import pool
from coop_cell_ko_poison import pos, maker_pos
from coop_cell_puppet_scale import playing, wait
import settings_lib as sl
from twoinst import Checks, start


def clouds_near(con, at=None, far_from=None):
    """keys of the poison cells this game shows (nearest first); only those within 3 m of `at` and more than 3 m
    from `far_from` when given"""
    out = con.call('coop.pool see 400')['out']
    keys = [m.group(1) for m in re.finditer(r'^see (c\d+) poison ', out, re.M)]
    if at is None and far_from is None:
        return keys
    r = []
    for k in keys:
        p = pos(con, k)
        if p and (at is None or math.dist(p, at) < 3.0) and (far_from is None or math.dist(p, far_from) > 3.0):
            r.append(k)
    return r


def partner_line(con):
    return next((x for x in con.call('coop.pool')['out'].splitlines() if x.startswith('cell_partner_poison')), '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--who', choices=('guest', 'host'), default='guest')
    ap.add_argument('--ff', action='store_true')
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    if a.ff:
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'friendly fire on for both')
        time.sleep(1)
    victim = guest if a.who == 'guest' else host
    hp_start = pool(victim)['hp']  # (before the part: the clouds start within a second or two)
    try:
        if a.who == 'guest':
            got = host.call('coop.pool borrow poison graft')['out'].strip()
            guest.call('coop.pool tp 3 0')
        else:
            got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
            guest.call('coop.pool tp 6 0')  # (the host's own clouds stay by the host)
        print('  host: ' + got, flush=True)
        ck(got.startswith('borrowed'), f'the {"host" if a.who == "guest" else "guest"}\'s cell has the poison part in the host\'s game')
        if not got.startswith('borrowed'):
            return ck.result()
        time.sleep(2)
        p0 = pool(victim)
        hp0, onto, used, t0 = hp_start, 0, set(), time.time()
        hp_min = min(x for x in (hp_start, p0['hp']) if x is not None) if p0['hp'] is not None else hp_start
        downed = p0['downed'] or 0
        print(f'  {a.who} before: hp {hp0}, {partner_line(victim)}', flush=True)
        while time.time() - t0 < 40 and onto < 10:
            if a.who == 'guest':
                keys = clouds_near(guest)
            else:
                keys = clouds_near(host, at=maker_pos(host, gid), far_from=maker_pos(host, 'host'))
            keys = [k for k in keys if k not in used]
            if keys:
                used.add(keys[0])
                r = victim.call(f'coop.pool swimto {keys[0]} tp 0.2')['out'].strip()
                if 'moved' in r:
                    onto += 1
            for _ in range(4):
                time.sleep(0.25)
                p = pool(victim)
                if p['hp'] is not None:
                    hp_min = min(hp_min, p['hp'])
                downed = max(downed, p['downed'] or 0)
            if a.who == 'host':  # (back next to the puppet: its clouds start there)
                host.call(f'coop.pool tp 3 0 {gid}')
        line = partner_line(victim)
        print(f'  {a.who}: put onto {onto} clouds of the partner in {time.time() - t0:.0f} s; hp {hp0} -> min {hp_min}, '
              f'knocked out {downed}; {line}', flush=True)
        print(f'  {a.who}: ' + pool(victim).__repr__(), flush=True)
        ck(onto >= 6, f'the {a.who} was put onto the partner\'s poison clouds ({onto} >= 6)')
        if a.ff:
            print(f'  (friendly fire on: the old behaviour, not judged; hp lost {(hp0 or 0) - (hp_min or 0)})', flush=True)
        else:
            ck(hp0 is not None and hp_min is not None and hp_min >= hp0 and not downed,
               f'the partner\'s poison does not hurt the {a.who}\'s cell in its own game (hp {hp0} -> min {hp_min}, knocked out {downed})')
    finally:
        host.call('coop.pool borrow off' if a.who == 'guest' else f'coop.pool borrow off x p{gid}')
        if a.ff:
            sl.force(host, [guest], 'all.friendly_fire', 'off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
