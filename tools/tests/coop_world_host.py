#!/usr/bin/env python3
"""World objects that exist differently in the two games: the host's game is the truth (0.7.9 L2, mods/coop/worldobj.cpp,
docs/re/creature.md#creature-host-places). Every case asserts the game's own object list on both machines (exists,
position), not the mod's bookkeeping:

  --case fruit      (BUG-0497, creature) a host fruit whose copy the guest's game lost (gone there only, as its flora's
                    recycling does), and a fruit the guest's game rolled elsewhere (moved 8.5 m there only): both lie
                    at the host's places in the guest's game (within 1.5 m), the rolled one not where it rolled to.
                    SPORE_FIX_OFF=wobj.fruit_place fails.

  coop_world_host.py --case fruit
"""
import argparse
import math
import time

from coop_world_objects import near, objs, pairs, wait
from twoinst import Checks, start


def fruit_at(con, pos, d=1.5):
    return [f for f in near(objs(con, 'fruit', 4, pos), pos, d) if f['st'] in (1, 2)]


def fruit(host, guest, ck):
    # 1. a host fruit whose copy the guest's game lost (its flora recycled it for another drop, Flora_TakeFruitForDrop):
    #    gone from the guest's game only, without a request to the host
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0, 60) if h['st'] == 2 and g['st'] == 2]
    print('  fruit on the ground on both games (host id, guest id, pos):', [(h['id'], g['id'], h['pos']) for h, g in ps][:4], flush=True)
    if not ck(len(ps) >= 2, 'two fruit on the ground at the same places on both games'):
        return
    h, g = ps[0]
    print('  guest:', guest.call(f'coop.wobj eat {g["p"]} local')['out'].strip(), f'(the guest\'s copy at {g["pos"]} gone, the host\'s stays)',
          flush=True)
    time.sleep(10)
    hf = next((f for f in objs(host, 'fruit', 4, h['pos']) if f['id'] == h['id']), None)
    if not ck(hf, 'the host\'s fruit is still there (the host\'s game)'):
        return
    # fruit lie in heaps: the same number within 1.5 m of it on both games
    n_host = len(fruit_at(host, hf['pos']))
    got = wait(lambda: len(fruit_at(guest, hf['pos'])) >= n_host, 10)
    print(f'  host fruit at {hf["pos"]}: {n_host} within 1.5 m on the host, {len(fruit_at(guest, hf["pos"]))} on the guest', flush=True)
    ck(got, 'the host\'s fruit whose copy the guest\'s game lost lies at its place in the guest\'s game again '
            '(as many fruit there as on the host)')
    # 2. a fruit the guest's game rolled elsewhere
    ps = [(h, g) for h, g in pairs(host, guest, 'fruit', 1.0, 60) if h['st'] == 2 and g['st'] == 2
          and math.dist(h['pos'], hf['pos']) > 3 and h['id'] != hf['id']]
    print('  fruit on the ground on both games:', [(h['id'], g['id'], h['pos']) for h, g in ps][:4], flush=True)
    if not ck(ps, 'a fruit on the ground at the same place on both games'):
        return
    h2, g2 = ps[0]
    away = (g2['pos'][0] + 6, g2['pos'][1], g2['pos'][2] + 6)
    print('  guest:', guest.call('coop.wobj move %s %.2f %.2f %.2f' % ((g2['p'],) + away))['out'].strip(),
          '(the guest\'s fruit rolled 8.5 m away)', flush=True)
    time.sleep(8)
    hf2 = next((f for f in objs(host, 'fruit', 6, h2['pos']) if f['id'] == h2['id']), None)
    if not ck(hf2, 'the host\'s fruit is still there (the host\'s game)'):
        return
    got = fruit_at(guest, hf2['pos'])
    extra = [f for f in near(objs(guest, 'fruit', 6, away), away, 1.5)]
    print(f'  guest fruit at the host\'s place: {got}; at the rolled place: {extra}', flush=True)
    ck(got, 'the fruit lies at the host\'s place in the guest\'s game')
    ck(not extra, 'no fruit of the guest\'s own at the place it rolled to')
    # the fruit the guest's game made came from a tree slot whose mask was put back: the trees keep their fruit
    time.sleep(2)
    tp = pairs(host, guest, 'tree', 0.5, 70)
    diff = [(h['pos'], h['st'], g['st']) for h, g in tp if h['st'] != g['st']]
    print(f'  trees within 70 m on both games: {len(tp)}, with different fruit: {diff[:4]}', flush=True)
    ck(tp and not diff, 'every tree near the players has the same fruit on both games (the made fruit took none)')


def bundles(con, r=200, at=None):
    return objs(con, 'game_bundle', r, at)


def bundle(host, guest, ck):
    import re
    me = re.search(r'me pos=\((\S+) (\S+) (\S+)\)', host.call('coop.census all 1')['out'])
    at = tuple(float(x) for x in me.groups())
    spot = (at[0] + 4, at[1], at[2] + 4)
    out = host.call('coop.wobj bundle ground %.2f %.2f %.2f 5' % spot)['out'].strip()
    print('  host:', out, '(food dropped on the ground)', flush=True)
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', out)
    if not ck('bundle 0' not in out and m, 'the host\'s game dropped a food bundle on the ground'):
        return
    hpos = tuple(float(x) for x in m.groups())
    got = wait(lambda: near(bundles(guest, 10, hpos), hpos, 1.5), 10)
    print(f'  guest bundles at the host\'s place {hpos}: {got}', flush=True)
    ck(got, 'the host\'s ground food lies at its place in the guest\'s game')
    # carried: a member of the host's tribe near the village with an empty hand
    rows = re.findall(r'([0-9A-F]{8}) alive=1 pos=\((\S+) (\S+) (\S+)\) dist=(\S+)', host.call('coop.tribe objs 18EB4B7')['out'])
    carried = None
    for p, x, y, z, d in rows[:12]:
        out = host.call(f'coop.wobj bundle carry {p} 5')['out'].strip()
        if not out.startswith('bundle 0') and 'bundle 00000000' not in out:
            carried = (p, (float(x), float(y), float(z)), out)
            break
    print('  host: a member carries food:', carried, flush=True)
    if ck(carried, 'a member of the host\'s tribe carries a food bundle (the host\'s game)'):
        time.sleep(1)
        hb = [b for b in bundles(host, 300) if b['cls'] & 0x100 and math.dist(b['pos'], tuple(float(v) for v in
              re.search(r'pos=\((\S+) (\S+) (\S+)\)', carried[2]).groups())) < 0.5]
        if ck(hb, 'the host\'s carried bundle is listed'):
            got = wait(lambda: [b for b in near(bundles(guest, 10, hb[0]['pos']), hb[0]['pos'], 1.5) if b['cls'] & 0x100], 12)
            print(f'  guest carried bundles at {hb[0]["pos"]}: {got}', flush=True)
            ck(got, 'the guest\'s copy of that member carries the food too (the guest\'s game: a bundle in a creature\'s container)')
    # the ground food taken in the guest's game
    g = near(bundles(guest, 10, hpos), hpos, 1.5)
    if g:
        print('  guest:', guest.call(f'coop.wobj bundle take {g[0]["p"]}')['out'].strip(), '(a guest\'s member took the food)', flush=True)
        gone = wait(lambda: not near(bundles(host, 10, hpos), hpos, 1.0), 10)
        ck(gone, 'the food the guest\'s member took is gone from the host\'s ground')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('fruit', 'bundle'), required=True)
    a = ap.parse_args()
    host, guest = start({'fruit': 'creature', 'bundle': 'tribe'}[a.case])
    ck = Checks()
    time.sleep(6)  # the first snapshots
    {'fruit': fruit, 'bundle': bundle}[a.case](host, guest, ck)
    print('  guest:', guest.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    print('  host:', host.call('coop.wobj')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
