#!/usr/bin/env python3
"""BUG-0384: the eggs at another species' nest on the host and on the guest (player report 2026-11-01:
"in enemy nests eggs are desynced and bugged (flying mid air)").

The eggs are cEgg objects (noun 0x2A034CD) of the nest's herd: a herd that misses members lets one of
them lay eggs, which hatch into new members minutes later (docs/re/creature.md#creature-nest-eggs).

1. Two games on the creature fixture; both avatars next to the nearest nest of another species.
2. The host's herd of that nest misses two members (`coop.nest target`, its respawn time 2 s): the
   game's own AI picks a layer, which walks up and lays the eggs (nothing forced but the shortfall).
3. Both games list the eggs near the nest (`coop.nest objs`: the game's egg objects, their position
   and height over the terrain). PASS when the guest has as many as the host, each host egg has a
   guest egg within 1 m and every guest egg lies on the ground or in the nest (0.8 m over the terrain
   in the nest's bowl, measured).
4. The guest eats one (`coop.nest eat`: what the game's eat node does to an egg, cEgg::SetState(4)):
   the host's egg is eaten or gone. The host eats another: the guest's is eaten or gone.

SPORE_FIX_OFF=nest.eggs fails: the guest has no eggs at the host's nests.

  coop_nest_eggs.py [--extra N]"""
import argparse
import math
import re
import time

from twoinst import Checks, shot, start

NEAR = 15.0     # m around the nest's home
MATCH = 1.0     # m between a host egg and the guest's
OG_MIN, OG_MAX = -0.5, 1.3  # m over the terrain: on the ground or in the nest bowl (0.74-0.8 m at its centre)

EGG = re.compile(r'egg (\S+) pos=\((\S+) (\S+) (\S+)\) over_ground=(\S+) alive=(\d) dist=\S+ state=(\d+) hatch=(-?\d+) '
                 r'herd=(\S+) family=(\d) serial=(\d+) mirror_of=(\d+)')


def eggs(con, home):
    out = []
    for m in EGG.finditer(con.call('coop.nest objs 60')['out']):
        e = {'p': m.group(1), 'pos': tuple(map(float, m.group(2, 3, 4))), 'og': float(m.group(5)), 'alive': int(m.group(6)),
             'state': int(m.group(7)), 'family': int(m.group(10)), 'serial': int(m.group(11)), 'of': int(m.group(12))}
        if not e['family'] and math.dist(e['pos'], home) < NEAR:
            out.append(e)
    return out


def nest_herds(con):
    """(dist, herd, members, home) of the nests of other species, nearest first"""
    family = set(re.findall(r'herd (\S+) nest=\S+ eggs=\S+ in_nest=\d+ layer=\S+ avatar=1', con.call('coop.nest herds')['out']))
    out = []
    for m in re.finditer(r'herd (\S+) arch=\S+ species=\S+ nest=(\S+) dist=(\d+) en=(\d) members=(\d+) in_world=(\d+) '
                         r'near_home=\d+ home=\((\S+) (\S+) (\S+)\)', con.call('coop.npc archless all')['out']):
        if m.group(2) in ('00000000', '(nil)') or m.group(1) in family or int(m.group(6)) < 3:
            continue
        out.append((int(m.group(3)), m.group(1), int(m.group(5)), tuple(map(float, m.group(7, 8, 9)))))
    return sorted(out)


def short(es):
    return [(e['p'], e['state'], round(e['og'], 2), e['serial'], e['of']) for e in es]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--extra', type=int, default=2, help='members the herd misses')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    ns = nest_herds(host)
    print('  nests of other species (dist, herd, members, home):', ns[:4], flush=True)
    if not ck(ns, 'a nest of another species with members on the host'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    dist, herd, members, home = ns[0]
    hx = '%.2f %.2f %.2f' % home
    print('  host:', host.call(f'coop.nest look {hx} 6')['out'].strip(), flush=True)
    print('  guest:', guest.call(f'coop.nest look {hx} 8')['out'].strip(), flush=True)
    time.sleep(3)
    print('  host:', host.call(f'coop.nest respawn {herd} 2000')['out'].strip(), '/',
          host.call(f'coop.nest target {herd} {members + a.extra}')['out'].strip(), flush=True)
    t0 = time.time()
    ho = []
    while time.time() - t0 < 90:
        ho = eggs(host, home)
        lay = re.search(r'eggs_to_lay=(-?\d+)', host.call(f'coop.nest herd {herd}')['out'])
        if len(ho) >= a.extra and lay and int(lay.group(1)) <= 0:
            break
        time.sleep(2)
    print(f'  {len(ho)} egg(s) on the host after {time.time() - t0:.0f} s', flush=True)
    time.sleep(6)  # the last egg settles, the next snapshot
    ho, go = eggs(host, home), eggs(guest, home)
    print('  host eggs (ptr, state, over ground, serial):', short(ho), flush=True)
    print('  guest eggs (ptr, state, over ground, serial, host egg):', short(go), flush=True)
    shot(host, 'eggs_host')
    shot(guest, 'eggs_guest')
    ck(ho, 'the host\'s herd laid eggs at its nest')
    ck(len(go) == len(ho), f'the guest has as many eggs at the nest as the host ({len(go)} / {len(ho)})')
    far = []
    for e in ho:
        d = min((math.dist(e['pos'], g['pos']) for g in go), default=99.0)
        if d > MATCH:
            far.append((e['serial'], round(d, 2)))
    ck(ho and not far, f'every host egg has a guest egg within {MATCH} m (missing: {far})')
    up = [(g['p'], round(g['og'], 2)) for g in go if not OG_MIN <= g['og'] <= OG_MAX]
    ck(go and not up, f'every guest egg lies on the ground or in the nest ({OG_MIN}..{OG_MAX} m over the terrain; off: {up})')
    print('  host eggs over the terrain:', [round(e['og'], 2) for e in ho], flush=True)

    # the guest eats one: the host's egg is eaten (state 4) or gone
    mine = [g for g in go if g['of'] and g['state'] in (5, 6)]
    eaten = None
    if ck(mine, 'the guest has an intact egg of the host\'s'):
        g = mine[0]
        eaten = g['of']
        print('  guest:', guest.call(f'coop.nest eat {g["p"]}')['out'].strip(), f'(host egg {eaten})', flush=True)
        st = None
        for _ in range(10):
            time.sleep(1)
            h = [e for e in eggs(host, home) if e['serial'] == eaten]
            st = h[0]['state'] if h else 'gone'
            if st in (4, 'gone'):
                break
        ck(st in (4, 'gone'), f'the egg the guest ate is eaten on the host (state {st})')
    # the host eats another: the guest's egg is eaten or gone
    rest = [e for e in eggs(host, home) if e['serial'] != eaten and e['state'] in (5, 6)]
    if ck(rest, 'the host has another intact egg'):
        e = rest[0]
        ck([x for x in eggs(guest, home) if x['of'] == e['serial']], f'the guest shows host egg {e["serial"]} before the host eats it')
        print('  host:', host.call(f'coop.nest eat {e["p"]}')['out'].strip(), f'(egg {e["serial"]})', flush=True)
        st = None
        for _ in range(10):
            time.sleep(1)
            g = [x for x in eggs(guest, home) if x['of'] == e['serial']]
            st = g[0]['state'] if g else 'gone'
            if st in (4, 'gone'):
                break
        ck(st in (4, 'gone'), f'the egg the host ate is eaten on the guest (state {st})')
    print('  guest:', guest.call('coop.nest')['out'].strip(), flush=True)
    print('  host:', host.call('coop.nest')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
