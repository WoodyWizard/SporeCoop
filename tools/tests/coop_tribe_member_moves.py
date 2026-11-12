#!/usr/bin/env python3
"""Tribe: does a member of a village join the player tribe at an alliance or a conquest? (BUG-0841, evidence)

The host gives a member its owner only when the member first shows in the host's table; a member that moved from an
NPC tribe into the player tribe would keep owner 0xFE (254) and nobody could select it. The game's code adds only new
citizens to a tribe (docs/re/tribe.md#tribe-member-moves); this test looks at the game for the two ways a village
ends in Tribe.

1. Two games on the tribe fixture. The host's game forms an NPC tribe 45 m away (`coop.tribe npc_tribe`) and its
   feelings for the player tribe go to the top (the game's ApplyRelationship, `coop.wsync rel <pol> 30`): the alliance.
2. A second NPC tribe 45 m the other way is raided by the players' members until its hut falls (the conquest of
   coop_tribe_conquest_guest.py).
3. After each: the player tribe's members in the host's game (the game's own list) and their owners, the members the
   host's table saw changing tribes (`member_moves=<all>/<into the player tribe unowned>`).
PASS: no member of the player tribe has owner 254 and no member changed its tribe.
"""
import re
import time

from coop_tribe_conquer_orders import HUT, huts
from coop_tribe_gather_stall import pos_of
from coop_tribe_tools import dist, members
from twoinst import Checks, start


def owners(con):
    """{member id: owner} of the player tribe (every member the game lists, babies and the dead too)"""
    out, inside = {}, False
    for ln in con.call('coop.tribe')['out'].splitlines():
        if ln.startswith('tribe '):
            inside = '(player)' in ln
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+)', ln)
        if m and inside:
            out[int(m.group(1))] = int(m.group(2))
    return out


def moves(con):
    m = re.search(r'member_moves=(\d+)/(\d+)', con.call('coop.tribe')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else None


def rels(con):
    out = {}
    for ln in con.call('coop.wsync dump tribe_rel')['out'].splitlines():
        m = re.search(r'key=([0-9A-F]+) rel=([-\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln)
        if m:
            out[m.group(1)] = (float(m.group(2)), tuple(float(m.group(i)) for i in (3, 4, 5)))
    return out


def cinematics(con):
    return [ln.strip() for ln in con.call('coop.status')['out'].splitlines() if 'cinematic' in ln][:2]


def form(host, k):
    out = host.call(f'coop.tribe npc_tribe 3 {k}')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def report(host, tag, ck):
    o, mv = owners(host), moves(host)
    print(f'  {tag}: player tribe {o}; member_moves {mv}', flush=True)
    ck(o and all(v != 254 for v in o.values()), f'{tag}: every member of the player tribe has a player as its owner ({o})')
    ck(mv is not None and mv[1] == 0, f'{tag}: no member came into the player tribe unowned ({mv})')
    return o, mv


def conquer(host, guest, tp, ck):
    before = {h for h, _ in huts(host)}
    time.sleep(3)
    new = sorted((dist(p, tp), h, p) for h, p in huts(host) if h not in before or dist(p, tp) < 25)
    if not ck(bool(new), f'the village to conquer has a hut ({len(new)})'):
        return False
    _, _, hp = new[0]
    mem = members(guest)
    raiders = sorted(i for i, o in mem.items() if o != 0)[:3] + sorted(i for i, o in mem.items() if o == 0)[-1:]
    for mid in raiders:
        host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))
    t0, cut, last_hp, last_change = time.time(), False, None, time.time()
    while time.time() - t0 < 150:
        time.sleep(2)
        hs = host.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        mm = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
        h, state = (float(mm.group(1)), int(mm.group(2))) if mm else (None, None)
        nearby = [mid for mid in raiders if pos_of(host, mid) and dist(pos_of(host, mid), hp) < 14]
        if hs.startswith('no hut') or state == 2:
            print(f'  the hut fell after {time.time() - t0:.0f} s', flush=True)
            return True
        if h != last_hp:
            last_hp, last_change = h, time.time()
        elif cut and time.time() - last_change > 16:
            for mid in raiders:
                host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))
            last_change = time.time()
        if cut and h is not None and h > 10:
            cut = False
        if not cut and len(nearby) >= max(1, len(raiders) // 2):
            host.call('coop.tribe huthp %.2f %.2f %.2f 2' % hp)
            cut = True
    return False


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    report(host, 'start', ck)

    # the alliance
    r0 = rels(host)
    tp = form(host, 45)
    ck(tp is not None, 'the host formed a village for the alliance')
    time.sleep(4)
    r1 = rels(host)
    new = [k for k in r1 if k not in r0]
    ck(bool(new), f'the new village has a relation row ({new})')
    for k in new[:1]:
        print('  host: ' + host.call(f'coop.wsync rel {k} 30')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 40:
        time.sleep(3)
        print(f'  {time.time() - t0:3.0f} s: {cinematics(host)}', flush=True)
        host.call('key enter')
    print(f'  relation now {rels(host).get(new[0]) if new else None}', flush=True)
    report(host, 'after the alliance', ck)

    # the conquest
    tp2 = form(host, -45)
    ck(tp2 is not None, 'the host formed a village to conquer')
    if tp2 and ck(conquer(host, guest, tp2, ck), 'the raid ended the village\'s hut'):
        t0 = time.time()
        while time.time() - t0 < 30:
            time.sleep(3)
            host.call('key enter')
        report(host, 'after the conquest', ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
