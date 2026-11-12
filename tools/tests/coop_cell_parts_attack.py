#!/usr/bin/env python3
"""Cell: a guest's electric part and poison part hurt the host's creatures (BUG-0715, BUG-0716, class BUG-0428).

The electric part's chain attack (0xE71520) writes the targets' hp itself (no DamageCell): on a guest the
targets are mirrors of the host's creatures, so their hp came back with the host's next state and the
host's creatures took nothing. A poison cloud is a cell the guest's own game makes for its avatar; its
hits on a mirror (DamageCell with the cloud as the attacker) were dropped: only the avatar's counted.

The guest's avatar borrows the model of a cell in the pool with that part (`coop.pool borrow electric|poison`:
the game's own electric step 0xE71980 / poison step 0xE76880 then run for it; none in the pool: `... graft`, the
part written into the avatar's own model data as a creation with it has), next to a host creature
given hp 60 (`coop.pool npcflag`).
1. --part electric: within --seconds the host's game applies type-1 hits of the guest (`guest_attack_types=`
   of the host's `coop.pool`, the damage type of the electric animation) and the creature's hp drops in
   the HOST's game; the guest's game zapped (`zaps=`).
2. --part poison: the guest's creation has the poison part in the host's game (`borrow poison graft p<id>` on the
   host: the puppet's model data), the guest stays next to the creature: the host's game makes the guest's clouds
   (`puppet_clouds=`). (Its damage is the game's: with the grafted values even the host's own avatar's clouds hurt
   nothing, lab 20261108-044202, so the creature's hp is only printed.)
SPORE_FIX_OFF=cell.electric (--part electric) / cell.puppet_poison (--part poison) must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_cell_parts_attack.py --part electric|poison [--seconds 40]
"""
import argparse
import math
import re
import time

from twoinst import Checks, start


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def clouds(con):
    """keys of the poison cells this game has on its screen"""
    out = con.call('coop.pool see 400')['out']
    return {m.group(1) for m in re.finditer(r'^see (c\d+) poison ', out, re.M)}


def cell_pos(con, key):
    m = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call(f'coop.pool cell {key}')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def puppet_pos(host, gid):
    """where the host's game has the guest's puppet (coop.pool scalepos: p<id> rows)"""
    m = re.search(r'\bp' + str(gid) + r'=\((-?[\d.]+) (-?[\d.]+)\)', host.call('coop.pool scalepos')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def types(host):
    m = re.search(r'guest_attack_types=([\d/]+)', host.call('coop.pool')['out'])
    return [int(x) for x in m.group(1).split('/')] if m else [0] * 6


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', choices=('electric', 'poison'), default='electric')
    ap.add_argument('--seconds', type=float, default=40)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    got = ''
    gm = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gm.group(1) if gm else '1'
    for _ in range(20):  # a cell of the pool with that part (mirrors come in as the guest looks around)
        if a.part == 'poison':  # the host makes a guest's clouds: the guest's creation as the host's game has it
            got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
        else:
            got = guest.call(f'coop.pool borrow {a.part}' + (' graft' if _ >= 6 else ''))['out'].strip()
        if got.startswith('borrowed'):
            break
        if a.part != 'poison':
            guest.call(f'coop.pool tp {(_ % 4 - 1.5) * 40:.0f} {(_ % 3 - 1) * 40:.0f} 0')
        time.sleep(2)
    print(('  host: ' if a.part == 'poison' else '  guest: ') + got, flush=True)
    ck(got.startswith('borrowed'), f'the guest\'s avatar has the game\'s {a.part} part ({got})')
    if not got.startswith('borrowed'):
        return ck.result()
    key, r = None, ''
    for _ in range(15):
        r = guest.call('coop.pool kill - hit')['out']  # names the nearest creature (one bite)
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'a host creature next to the guest ({r.strip()[:100]})')
    if not key:
        return ck.result()
    print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'].strip())
    if a.part == 'poison':  # the host's own cell (it shares the fixture's model and so the graft) 14 m off: its camera
        # still shows the guest's puppet (the poison step needs the puppet's GFX), its own clouds stay apart
        print('  host: ' + host.call('coop.pool tp 14 0')['out'].strip(), flush=True)
    time.sleep(2.5)
    t0_types, g0 = types(host), guest.call('coop.pool')['out']
    hp0 = num(host.call(f'coop.pool cell {key}')['out'], 'hp')
    low, t0, k0 = hp0, time.time(), 0
    placed, seen_clouds = 0, set()
    while time.time() - t0 < a.seconds:
        guest.call(f'coop.pool swimto {key} tp')  # stays next to it (the electric step needs a target near its parts)
        k0 += 1
        if a.part == 'poison':
            # BUG-0892: the creature onto each new cloud the host's game made next to the guest's puppet (the host's
            # own cell is 14 m away): the game's own contact of a creature with a partner's cloud decides the damage
            if k0 % 6 == 0:
                host.call('coop.pool tp 14 0')
            pp = puppet_pos(host, gid)
            for ck_key in sorted(clouds(host) - seen_clouds):
                seen_clouds.add(ck_key)
                cp = cell_pos(host, ck_key)
                if pp and cp and math.hypot(cp[0] - pp[0], cp[1] - pp[1]) < 5:
                    r = host.call(f'coop.pool place {key} {ck_key}')['out'].strip()
                    placed += 1
                    if placed <= 3:
                        print(f'  host: {r}', flush=True)
                    time.sleep(0.4)
        time.sleep(1.5 if a.part == 'electric' else 0.5)
        st = host.call(f'coop.pool cell {key}')['out']
        hp = num(st, 'hp')
        if hp is not None and low is not None:
            low = min(low, hp)
        if 'no such cell' in st or 'dying=1' in st or (low is not None and hp0 is not None and low <= hp0 - 6):
            break
    t1_types, g1 = types(host), guest.call('coop.pool')['out']
    print('  guest: ' + guest.call('coop.pool borrow status')['out'].strip(), flush=True)
    hs = host.call(f'coop.pool borrow status x p{gid}')['out'].strip()
    print('  host: ' + hs, flush=True)
    dt = [b - x for b, x in zip(t1_types, t0_types)]
    print(f'  host: hp {hp0} -> {low}, guest attack types +{dt}', flush=True)
    for k in ('zaps', 'zap_hits', 'product_hits', 'product_pairs', 'products', 'hits_sent'):
        print(f'  guest {k}: {num(g0, k)} -> {num(g1, k)}', flush=True)
    m = re.search(r'foreign_hits=([\d/]+)', g1)
    print(f'  guest foreign_hits (mirror/puppet/other): {m.group(1) if m else "?"}', flush=True)
    if a.part == 'electric':
        ck((num(g1, 'zaps') or 0) > (num(g0, 'zaps') or 0), '1. the guest\'s game ran its electric attack')
        ck(dt[1] > 0, f'1. the host\'s game applied the guest\'s electric hits (type 1: +{dt[1]})')
    else:
        ck((num(hs, 'puppet_clouds') or 0) > 0, f'2. the host\'s game made the guest\'s poison clouds ({num(hs, "puppet_clouds")})')
    if a.part == 'electric':
        ck(hp0 is not None and low is not None and low < hp0, f'the creature lost health in the host\'s game ({hp0} -> {low})')
    else:  # BUG-0892: the damage in the host's game, not the cloud counter
        print(f'  creature hp in the host\'s game {hp0} -> {low}; {placed} times put onto the guest\'s clouds; the host '
              f'avatar\'s own clouds {num(hs, "clouds_made")}', flush=True)
        ck(placed > 0, f'2. the creature was put onto the guest\'s clouds in the host\'s game ({placed} times)')
        ck(hp0 is not None and low is not None and low < hp0,
           f'2. the guest\'s clouds took health off the creature in the host\'s game ({hp0} -> {low})')
        gst = guest.call(f'coop.pool cell {key}')['out']
        ghp = num(gst, 'hp')
        print(f'  the guest\'s mirror: hp {ghp} ({gst.strip()[:120]})', flush=True)
        ck('no such cell' in gst or 'dying=1' in gst or (ghp is not None and low is not None and ghp <= low + 1),
           f'2. the guest\'s mirror shows the loss (hp {ghp}, host {low})')
    guest.call('coop.pool borrow off')
    host.call(f'coop.pool borrow off x p{gid}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
