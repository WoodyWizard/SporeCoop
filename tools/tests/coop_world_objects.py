#!/usr/bin/env python3
"""World objects owned by the host (docs/sync-architecture-plan.md L1 / L2, mods/coop/worldobj.cpp, nest.cpp).

Each kind is checked in the other player's game (the game's own object list and state), both ways where the
kind has two ways:

  --kind ornament  (BUG-0459 / BUG-0473, creature) the guest uses a bone that gives a part (the game's
                   interact end: 0xC40360, +0xA3, message 0xD3353638 -> the reward); the host's copy of it is
                   used up (its reward +0x24), and when the host's avatar uses it too nobody gets a second
                   part: both games have one part more than before, not two.
                   SPORE_FIX_OFF=wobj.ornament fails.
  --kind fruit     (BUG-0472, creature) the host eats a fruit on the ground (cFruit_SetState 5, the eat node's
                   end): the guest's copy is gone from the guest's game; the guest eats another: the host's is
                   gone. SPORE_FIX_OFF=wobj.fruit fails.
  --kind tree      (BUG-0472, creature) a fruit falls from a tree in the host's game (the flora's DropFruit):
                   the same slot falls in the guest's game (its tree's fruit mask, a fruit on the ground near
                   it); the other way round from the guest. SPORE_FIX_OFF=wobj.tree fails.
  --kind herd      (BUG-0460, creature) the host's avatar kills a species at its nest until the species is
                   extinct (the game's own TakeDamage -> ApplyEvent -> 0xBA4750): the nest is extinct in the
                   guest's game too (herd +0x190, nest +0x120, what the minimap shows).
                   SPORE_FIX_OFF=wobj.herd fails.
  --kind eggs      (BUG-0474, tribe) the eggs at the animal nests in the host's game are in the guest's game
                   at the same places (coop.census all: the game's noun list). SPORE_FIX_OFF=nest.eggs_tribe
                   fails.

  coop_world_objects.py --kind ornament|fruit|tree|herd|eggs
"""
import argparse
import math
import re
import time

import worlddiff
from twoinst import Checks, start

ROW = re.compile(r'(\S+) (\S+) id=(\d+) pos=\((\S+) (\S+) (\S+)\) st=(\S+) dist=(\S+) bound_to=(\d+)')


def objs(con, kind, r=80, at=None):
    cmd = f'coop.wobj list {kind} {r}' + (' %.2f %.2f %.2f' % at if at else '')
    out = []
    for m in ROW.finditer(con.call(cmd)['out']):
        out.append({'p': m.group(2), 'id': int(m.group(3)), 'pos': tuple(map(float, m.group(4, 5, 6))),
                    'st': int(m.group(7), 16), 'bound': int(m.group(9))})
    return out


def near(lst, pos, d):
    return [o for o in lst if math.dist(o['pos'], pos) < d]


def parts(con):
    m = re.search(r'parts=(\d+)', con.call('coop.parts')['out'])
    return int(m.group(1)) if m else -1


def pairs(host, guest, kind, d, r=60):
    """objects of a kind near both players that the two games have at the same place (within d m)"""
    ho, go = objs(host, kind, r), objs(guest, kind, r)
    out = []
    for h in ho:
        g = sorted(near(go, h['pos'], d), key=lambda g: math.dist(g['pos'], h['pos']))
        if g:
            out.append((h, g[0]))
    return out


def wait(fn, t=10.0):
    t0 = time.time()
    while True:
        v = fn()
        if v or time.time() - t0 > t:
            return v
        time.sleep(1)


def ornament(host, guest, ck):
    ps = pairs(host, guest, 'interactive', 1.5, 90)
    print('  bones with a reward on both games (host id, guest id, host st, guest st):',
          [(h['id'], g['id'], h['st'], g['st']) for h, g in ps], flush=True)
    ps = [(h, g) for h, g in ps if h['st'] == 0 and g['st'] == 0]
    if not ck(ps, 'a bone with a part reward, unused, at the same place on both games'):
        return
    h, g = ps[0]
    before = parts(host), parts(guest)
    print('  parts before (host, guest):', before, flush=True)
    print('  guest:', guest.call(f'coop.wobj use {g["p"]}')['out'].strip(), flush=True)
    used = wait(lambda: [x for x in near(objs(host, 'interactive', 3, h['pos']), h['pos'], 0.5) if x['st'] & 1], 8)
    ck(used, 'the bone the guest used is used up in the host\'s game (its reward +0x24)')
    time.sleep(4)  # parts.shared: the guest's find reaches the host
    print('  host:', host.call(f'coop.wobj use {h["p"]}')['out'].strip(), '(the host\'s avatar uses the same bone)', flush=True)
    time.sleep(12)  # parts.shared: a find of the host's reaches the guest
    after = parts(host), parts(guest)
    print('  parts after (host, guest):', after, flush=True)
    ck(after == (before[0] + 1, before[1] + 1), f'one bone, one part on each game (host {before[0]} -> {after[0]}, '
                                                  f'guest {before[1]} -> {after[1]})')


def fruit(host, guest, ck):
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0) if h['st'] == 2 and g['st'] == 2]
    print('  fruit on the ground on both games (host id, guest id, pos):', [(h['id'], g['id'], h['pos']) for h, g in ps], flush=True)
    if not ck(len(ps) >= 2, 'two fruit on the ground at the same places on both games'):
        return
    (h1, g1), (h2, g2) = ps[0], ps[1]
    print('  host:', host.call(f'coop.wobj eat {h1["p"]}')['out'].strip(), f'(fruit at {h1["pos"]})', flush=True)
    gone = wait(lambda: not near(objs(guest, 'fruit', 3, g1['pos']), g1['pos'], 0.5), 6)
    ck(gone, 'the fruit the host ate is gone from the guest\'s game')
    print('  guest:', guest.call(f'coop.wobj eat {g2["p"]}')['out'].strip(), f'(fruit at {g2["pos"]})', flush=True)
    gone = wait(lambda: not near(objs(host, 'fruit', 3, h2['pos']), h2['pos'], 0.5), 6)
    ck(gone, 'the fruit the guest ate is gone from the host\'s game')


def tree(host, guest, ck):
    ps = [(h, g) for h, g in pairs(host, guest, 'tree', 0.3) if h['st'] == g['st'] and bin(h['st']).count('1') >= 2]
    print('  trees with fruit on both games (host mask, guest mask, pos):', [(h['st'], g['st'], h['pos']) for h, g in ps][:6], flush=True)
    if not ck(len(ps) >= 2, 'two trees with fruit, the same fruit left on both games'):
        return
    for who, src, dst, (a, b) in (('host', host, guest, ps[0]), ('guest', guest, host, ps[1][::-1])):
        slot = min(i for i in range(32) if a['st'] >> i & 1)
        print(f'  {who}:', src.call(f'coop.wobj drop {a["p"]} {slot}')['out'].strip(), f'(slot {slot} of the tree at {a["pos"]})', flush=True)
        other = 'guest' if who == 'host' else 'host'
        fell = wait(lambda: [t for t in objs(dst, 'tree', 2, b['pos']) if not t['st'] >> slot & 1], 6)
        ck(fell, f'the fruit fell from the same tree in the {other}\'s game too (mask {b["st"]:X} -> '
                 f'{fell[0]["st"]:X})' if fell else f'the fruit fell from the same tree in the {other}\'s game too')
        down = near(objs(dst, 'fruit', 12, b['pos']), b['pos'], 8)
        ck(down, f'a fruit lies under that tree in the {other}\'s game ({len(down)})')


def herd(host, guest, ck):
    from coop_nest_eggs import nest_herds
    ns = nest_herds(host)
    print('  nests of other species (dist, herd, members, home):', ns[:4], flush=True)
    if not ck(ns, 'a nest of another species with members on the host'):
        return
    dist, h, members, home = ns[0]
    hs = lambda con: near(objs(con, 'herd', 5, home), home, 1.0)
    print('  before: host', hs(host), 'guest', hs(guest), flush=True)
    ck(hs(guest) and not hs(guest)[0]['st'] & 1, 'the guest has that nest, not extinct')
    for _ in range(4):  # the species dies out: its creatures killed by the host's avatar (TakeDamage -> ApplyEvent)
        print('  host:', host.call(f'coop.nest kill {h} 20')['out'].strip(), flush=True)
        if wait(lambda: [x for x in hs(host) if x['st'] & 1], 4):
            break
    hx = [x for x in hs(host) if x['st'] & 1]
    ck(hx, 'the nest is extinct in the host\'s game (herd +0x190)')
    want = hx[0]['st'] if hx else 1
    ext = wait(lambda: [x for x in hs(guest) if x['st'] & want == want], 8)
    print('  after: host', hs(host), 'guest', hs(guest), flush=True)
    ck(ext, f'the nest is extinct in the guest\'s game too (the host\'s state {want}: herd +0x190 bit 1, nest +0x120 bit 2)')


def eggs(host, guest, ck):
    t0, hw, gw = time.time(), None, None
    while time.time() - t0 < 120:
        hw, gw = worlddiff.pair_dump(host, guest)
        he = [r for r in hw.rows if r['noun'] == 0x02A034CD]
        if he:
            break
        time.sleep(5)
    time.sleep(4)  # a snapshot of the eggs
    hw, gw = worlddiff.pair_dump(host, guest)
    he = [r for r in hw.rows if r['noun'] == 0x02A034CD]
    ge = [r for r in gw.rows if r['noun'] == 0x02A034CD]
    print(f'  eggs: host {len(he)}, guest {len(ge)} (nouns: host {hw.nouns.get(0x02A034CD, 0)}, guest {gw.nouns.get(0x02A034CD, 0)})', flush=True)
    if not ck(he, 'the host\'s game has eggs at the animal nests'):
        return
    miss = [r['pos'] for r in he if not any(math.dist(r['pos'], g['pos']) < 1.5 for g in ge)]
    ck(not miss, f'every egg of the host\'s is in the guest\'s game within 1.5 m (missing {len(miss)} of {len(he)})')
    print('  guest:', guest.call('coop.nest')['out'].strip(), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kind', choices=('ornament', 'fruit', 'tree', 'herd', 'eggs'), required=True)
    a = ap.parse_args()
    host, guest = start('tribe' if a.kind == 'eggs' else 'creature')
    ck = Checks()
    time.sleep(6)  # the first snapshots
    {'ornament': ornament, 'fruit': fruit, 'tree': tree, 'herd': herd, 'eggs': eggs}[a.kind](host, guest, ck)
    print('  guest:', guest.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    print('  host:', host.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
